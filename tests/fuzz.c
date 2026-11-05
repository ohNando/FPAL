// SPDX-License-Identifier: Apache-2.0
/*
 * A small mutation fuzzer for everything that parses data we do not control:
 * D-Bus messages from any local user, responses from the sensor, key presses,
 * and the files in /var/lib/fpal.
 *
 *   make fuzz                    full run (a few seconds)
 *   make fuzz FUZZ_PERCENT=5     a short run, used by CI
 *
 * Every input sits right in front of (or behind) an inaccessible page, so even
 * one byte of over-read or over-write faults. Build it with
 * CFLAGS_EXTRA='-fsanitize=address,undefined' to also catch the rest. The
 * generator is seeded, so a failure repeats.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../daemon/fprint_service.h"
#include "../tools/finger.h"
#include "../tools/manage.h"
#include "../tools/status.h"
#include "core/bytes.h"
#include "core/driver.h"
#include "dbus/conn.h"
#include "dbus/marshal.h"
#include "dbus/message.h"
#include "drivers/egis_moc/egis_moc_proto.h"
#include "fpal/bindings.h"
#include "fpal/labels.h"

static uint64_t rng = 88172645463325252ull;

static uint32_t rnd(void) {
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return (uint32_t)(rng >> 11);
}

/* ---- guarded buffers ----------------------------------------------------- */

struct guarded {
	uint8_t *p;
	void *base;
	size_t total;
};

static struct guarded guard_alloc(const uint8_t *src, size_t len, bool at_end) {
	const size_t page = 4096;
	const size_t body = ((len + page - 1) / page + 1) * page;
	const size_t total = body + 2 * page;
	uint8_t *base = mmap(NULL, total, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	struct guarded g = { NULL, base, total };

	if (base == MAP_FAILED) {
		perror("mmap");
		exit(2);
	}
	mprotect(base, page, PROT_NONE);
	mprotect(base + total - page, page, PROT_NONE);
	g.p = at_end ? base + total - page - len : base + page;
	if (len)
		memcpy(g.p, src, len);
	return g;
}

static void guard_free(struct guarded *g) {
	munmap(g->base, g->total);
}

/* ---- mutation ------------------------------------------------------------ */

static size_t mutate(uint8_t *b, size_t len, size_t cap) {
	static const uint32_t edge[] = { 0, 1, 0x7fffffff, 0x80000000, 0xffffffff,
					 0xfffffffe, 0x3fff, 0x4000, 0x4001, 255, 256 };

	for (int n = 1 + (int)(rnd() % 4); n > 0; n--) {
		size_t i;

		switch (rnd() % 9) {
		case 0: /* flip a bit */
			if (len)
				b[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8));
			break;
		case 1: /* replace a byte */
			if (len)
				b[rnd() % len] = (uint8_t)rnd();
			break;
		case 2: /* insert a byte */
			if (len < cap) {
				i = len ? rnd() % (len + 1) : 0;
				memmove(b + i + 1, b + i, len - i);
				b[i] = (uint8_t)rnd();
				len++;
			}
			break;
		case 3: /* delete a byte */
			if (len) {
				i = rnd() % len;
				memmove(b + i, b + i + 1, len - i - 1);
				len--;
			}
			break;
		case 4: /* truncate */
			if (len)
				len = rnd() % (len + 1);
			break;
		case 5: /* a length field with an edge value */
			if (len >= 4) {
				uint32_t x = edge[rnd() % (sizeof(edge) / sizeof(edge[0]))];

				i = rnd() % (len - 3);
				memcpy(b + i, &x, 4);
			}
			break;
		case 6: /* repeat a chunk */
			if (len && len * 2 < cap) {
				size_t l;

				i = rnd() % len;
				l = 1 + rnd() % (len - i);
				memcpy(b + len, b + i, l);
				len += l;
			}
			break;
		case 7: /* overwrite two bytes */
			if (len >= 2) {
				uint16_t x = (uint16_t)rnd();

				i = rnd() % (len - 1);
				memcpy(b + i, &x, 2);
			}
			break;
		default: /* a zero or 0xff byte */
			if (len)
				b[rnd() % len] = rnd() % 3 ? 0 : 0xff;
			break;
		}
	}
	return len;
}

/* ---- seeds: valid messages to start from ---------------------------------- */

#define NSEEDS 8
static uint8_t seeds[NSEEDS][1024];
static size_t seed_len[NSEEDS];
static int nseeds;

static void add_seed(const struct fpal_dmsg *m) {
	size_t n;

	if (nseeds < NSEEDS &&
	    fpal_dmsg_build(seeds[nseeds], sizeof(seeds[0]), &n, m) == FPAL_OK)
		seed_len[nseeds++] = n;
}

static void make_seeds(void) {
	uint8_t body[256];
	struct fpal_dw w;
	struct fpal_dmsg get = { .type = FPAL_DMSG_CALL, .serial = 3,
				 .path = FPAL_FPRINT_DEVICE_PATH,
				 .iface = "org.freedesktop.DBus.Properties",
				 .member = "Get", .sender = ":1.7",
				 .dest = "net.reactivated.Fprint", .sig = "ss" };
	struct fpal_dmsg call = { .type = FPAL_DMSG_CALL, .serial = 4,
				  .path = FPAL_FPRINT_MANAGER_PATH,
				  .iface = FPAL_FPRINT_MANAGER_IFACE,
				  .member = "GetDefaultDevice", .sender = ":1.9" };
	struct fpal_dmsg ret = { .type = FPAL_DMSG_RETURN, .serial = 5,
				 .reply_serial = 2, .sig = "a{sv}", .sender = ":1.1" };
	struct fpal_dmsg err = { .type = FPAL_DMSG_ERROR, .serial = 6, .reply_serial = 3,
				 .errname = "a.b.C", .sig = "s" };
	struct fpal_dw_arr arr;

	fpal_dw_init(&w, body, sizeof(body));
	fpal_dw_str(&w, FPAL_FPRINT_DEVICE_IFACE);
	fpal_dw_str(&w, "scan-type");
	get.body = body;
	get.body_len = (uint32_t)w.len;
	add_seed(&get);

	get.member = "GetAll";
	get.sig = "s";
	fpal_dw_init(&w, body, sizeof(body));
	fpal_dw_str(&w, FPAL_FPRINT_DEVICE_IFACE);
	get.body_len = (uint32_t)w.len;
	add_seed(&get);

	add_seed(&call);

	fpal_dw_init(&w, body, sizeof(body));
	fpal_dw_array_begin(&w, 8, &arr);
	fpal_dw_align(&w, 8);
	fpal_dw_str(&w, "name");
	fpal_dw_variant(&w, "s");
	fpal_dw_str(&w, "FPAL");
	fpal_dw_array_end(&w, &arr);
	ret.body = body;
	ret.body_len = (uint32_t)w.len;
	add_seed(&ret);

	fpal_dw_init(&w, body, sizeof(body));
	fpal_dw_str(&w, "oops");
	err.body = body;
	err.body_len = (uint32_t)w.len;
	add_seed(&err);
}

/* a valid sensor response (list of nothing, status 90 00) */
static size_t make_egis_seed(uint8_t *out) {
	static const uint8_t base[] = { 'S', 'I', 'G', 'E', 0, 0, 0, 1, 0, 0,
					0, 0, 0, 4, 0, 1, 0x90, 0 };
	uint16_t ck;

	memcpy(out, base, sizeof(base));
	ck = egis_checksum(out, sizeof(base));
	fpal_put_be16(out + 8, ck);
	return sizeof(base);
}

/* ---- targets ------------------------------------------------------------- */

static unsigned long parsed_ok;

static void t_dbus_message(const uint8_t *b, size_t len) {
	struct fpal_dmsg m;
	uint64_t frame;
	size_t total;

	if (len >= FPAL_DMSG_FIXED) {
		(void)fpal_dmsg_total_len(b, &total);
		(void)fpal_dmsg_frame_len(b, &frame);
	}
	if (fpal_dmsg_parse(b, len, &m) == FPAL_OK) {
		struct fpal_dr r;

		parsed_ok++;
		fpal_dr_init(&r, m.body, m.body_len);
		for (int i = 0; i < 6 && !r.err; i++) {
			switch (rnd() % 4) {
			case 0:
				(void)fpal_dr_str(&r);
				break;
			case 1:
				(void)fpal_dr_u32(&r);
				break;
			case 2:
				(void)fpal_dr_sig(&r);
				break;
			default:
				(void)fpal_dr_array_begin(&r, 8);
			}
		}
		if (m.path)
			(void)strlen(m.path);
		if (m.sig)
			(void)strlen(m.sig);
	}
}

static void t_sensor_response(const uint8_t *b, size_t len) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	struct egis_rsp rsp;
	size_t n;

	if (egis_parse(b, len, &rsp) != FPAL_OK)
		return;
	(void)egis_parse_list(&rsp, ids, FPAL_MAX_TEMPLATES, &n);
#ifdef FPAL_ENABLE_ENROLL
	(void)egis_classify_capture(&rsp);
#endif
	if (rsp.body_len)
		(void)rsp.body[rsp.body_len - 1];
}

static void t_keys(const uint8_t *b, size_t len) {
	struct fpal_line l;
	struct fpal_key k;
	size_t off = 0;
	int guard = 0;

	fpal_line_set(&l, "x");
	while (off < len && guard++ < 1000) {
		size_t used = fpal_key_parse(b + off, len - off, (rnd() & 1) != 0, &k);

		if (!used)
			used = fpal_key_parse(b + off, len - off, true, &k);
		if (!used)
			break;
		if (used > len - off) {
			fprintf(stderr, "BUG: key parser used %zu of %zu bytes\n", used,
				len - off);
			abort();
		}
		(void)fpal_line_feed(&l, &k);
		off += used;
	}
	if (l.len >= sizeof(l.text) || l.text[l.len] != '\0') {
		fprintf(stderr, "BUG: the line editor broke its buffer\n");
		abort();
	}
}

static void t_text(const uint8_t *b, size_t len) {
	struct fpal_template_id ids[3];
	size_t idx, matches;
	char *s = malloc(len + 1);

	memcpy(s, b, len);
	s[len] = '\0'; /* our own call sites always pass NUL terminated text */
	memset(ids, 'A', sizeof(ids));
	(void)fpal_label_valid(s);
	(void)fpal_finger_find("/nonexistent", ids, 3, s, &idx, &matches);
	fpal_text_sanitize(s);
	free(s);
}

/* the daemon's request handler: any local user can send it anything */
static bool present(void *ud) {
	(void)ud;
	return (rnd() & 1) != 0;
}

static int handler_fd[2];
static struct fpal_dbus handler_bus;

static void fake_bus_setup(int fds[2], struct fpal_dbus *bus) {
	uint8_t buf[512], body[64];
	struct fpal_dw w;
	size_t n;
	struct fpal_dmsg hello = { .type = FPAL_DMSG_RETURN, .serial = 1, .reply_serial = 1,
				   .sig = "s" };

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds))
		exit(2);
	(void)!write(fds[1], "OK 0123456789abcdef0123456789abcdef\r\n", 37);
	fpal_dw_init(&w, body, sizeof(body));
	fpal_dw_str(&w, ":1.5");
	hello.body = body;
	hello.body_len = (uint32_t)w.len;
	fpal_dmsg_build(buf, sizeof(buf), &n, &hello);
	(void)!write(fds[1], buf, n);
	if (fpal_dbus_attach(bus, fds[0])) {
		fprintf(stderr, "cannot set up the fake bus\n");
		exit(2);
	}
}

static void drain(int fd) {
	char sink[8192];

	while (recv(fd, sink, sizeof(sink), MSG_DONTWAIT) > 0)
		;
}

static void t_handler(const uint8_t *b, size_t len) {
	static const char *const paths[] = { FPAL_FPRINT_MANAGER_PATH,
					     FPAL_FPRINT_DEVICE_PATH, "/",
					     "/net/reactivated/Fprint" };
	static const char *const ifaces[] = {
		"org.freedesktop.DBus.Properties", "org.freedesktop.DBus.Introspectable",
		"org.freedesktop.DBus.Peer", FPAL_FPRINT_DEVICE_IFACE,
		FPAL_FPRINT_MANAGER_IFACE, "x.y",
	};
	static const char *const members[] = {
		"Get", "GetAll", "Set", "Introspect", "Ping", "GetDevices",
		"GetDefaultDevice", "Claim", "VerifyStart", "DeleteEnrolledFingers",
		"ListEnrolledFingers", "Nope",
	};
	static const char *const sigs[] = { "ss", "s", "", "sv", "u", "ssv" };
	struct fpal_fprint_ops ops = { .sensor_present = present };
	struct fpal_dmsg call = { .type = FPAL_DMSG_CALL, .serial = 1 + rnd() % 100,
				  .sender = ":1.9", .path = paths[rnd() % 4],
				  .iface = rnd() % 8 ? ifaces[rnd() % 6] : NULL,
				  .member = members[rnd() % 12],
				  .sig = rnd() % 8 ? sigs[rnd() % 6] : NULL,
				  .body = b, .body_len = (uint32_t)len };

	(void)fpal_fprint_handle(&handler_bus, &call, &ops);
	drain(handler_fd[1]);
}

/* a hostile bus: random bytes into the receive path and the daemon loop */
static void t_stream(const uint8_t *b, size_t len) {
	struct fpal_fprint_ops ops = { .sensor_present = present };
	struct fpal_dbus c;
	struct fpal_dmsg m;
	int f[2];
	size_t off = 0;

	fake_bus_setup(f, &c);
	while (off < len) {
		ssize_t k = write(f[1], b + off, len - off > 700 ? 700 : len - off);

		if (k <= 0)
			break;
		off += (size_t)k;
	}
	close(f[1]);
	if (rnd() & 1) {
		for (int i = 0; i < 16; i++)
			if (fpal_dbus_recv(&c, 0, &m) != FPAL_OK &&
			    fpal_dbus_recv(&c, 0, &m) != FPAL_E_DROPPED)
				break;
	} else {
		(void)fpal_fprint_pump(&c, &ops);
	}
	fpal_dbus_close(&c);
}

/* the files in /var/lib/fpal and the PAM files fpal status reads */
static char fdir[64];

static void write_file(const char *path, const uint8_t *b, size_t len, mode_t mode) {
	FILE *f = fopen(path, "w");

	if (f) {
		fwrite(b, 1, len, f);
		fclose(f);
		chmod(path, mode);
	}
}

static void t_files(const uint8_t *b, size_t len) {
	struct fpal_template_id id, ids[FPAL_MAX_TEMPLATES];
	char path[128], label[FPAL_LABEL_MAX + 1];
	size_t n;

	memset(&id, 'A', sizeof(id));
	snprintf(path, sizeof(path), "%s/.labels", fdir);
	write_file(path, b, len, 0600);
	(void)fpal_labels_get(fdir, &id, label, sizeof(label));
	snprintf(path, sizeof(path), "%s/victim", fdir);
	write_file(path, b, len, 0600);
	(void)fpal_bindings_load(fdir, "victim", ids, FPAL_MAX_TEMPLATES, &n);
	snprintf(path, sizeof(path), "%s/pamfile", fdir);
	write_file(path, b, len, 0644);
	(void)fpal_file_has_module(path, "pam_fpal.so");
}

/* ---- the driver ---------------------------------------------------------- */

typedef void (*target_fn)(const uint8_t *, size_t);

struct target {
	const char *name;
	target_fn fn;
	bool seeded;	      /* start from the D-Bus seeds */
	bool sensor;	      /* start from the sensor seed */
	unsigned long iters;
};

int main(int argc, char **argv) {
	struct target targets[] = {
		{ "dbus-message-parse", t_dbus_message, true, false, 1500000 },
		{ "sensor-response-parse", t_sensor_response, false, true, 1500000 },
		{ "key-and-line-editor", t_keys, false, false, 1500000 },
		{ "text-helpers", t_text, false, false, 600000 },
		{ "fpald-request-handler", t_handler, false, false, 400000 },
		{ "hostile-bus-stream", t_stream, true, false, 150000 },
		{ "config-file-parsers", t_files, false, false, 8000 },
	};
	const unsigned percent = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 100;
	char tmpl[] = "/tmp/fpal-fuzz-XXXXXX";
	uint8_t egis[64];
	size_t egis_len;
	char cmd[128];

	if (argc > 2)
		rng ^= strtoull(argv[2], NULL, 10) * 0x9e3779b97f4a7c15ull;
	make_seeds();
	egis_len = make_egis_seed(egis);
	fake_bus_setup(handler_fd, &handler_bus);
	if (!mkdtemp(tmpl))
		return 2;
	snprintf(fdir, sizeof(fdir), "%s", tmpl);

	for (size_t t = 0; t < sizeof(targets) / sizeof(targets[0]); t++) {
		const unsigned long iters = targets[t].iters * percent / 100;

		for (unsigned long i = 0; i < iters; i++) {
			uint8_t buf[2048];
			size_t len;
			struct guarded g;

			if (targets[t].sensor && rnd() % 4) {
				memcpy(buf, egis, egis_len);
				len = mutate(buf, egis_len, 256);
			} else if (targets[t].seeded && rnd() % 4) {
				const int s = (int)(rnd() % (uint32_t)nseeds);

				memcpy(buf, seeds[s], seed_len[s]);
				len = mutate(buf, seed_len[s], 1024);
			} else {
				len = rnd() % 200;
				for (size_t k = 0; k < len; k++)
					buf[k] = (uint8_t)rnd();
				if (targets[t].fn == t_keys && (rnd() & 1))
					for (size_t k = 0; k + 2 < len; k += 7) {
						buf[k] = 0x1b;
						buf[k + 1] = '[';
					}
			}
			g = guard_alloc(buf, len, (rnd() & 1) != 0);
			targets[t].fn(g.p, len);
			guard_free(&g);
		}
		printf("%-24s %9lu inputs, no crash\n", targets[t].name, iters);
		fflush(stdout);
	}
	printf("valid D-Bus messages reached by mutation: %lu\n", parsed_ok);
	fpal_dbus_close(&handler_bus);
	snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpl);
	if (system(cmd) != 0)
		return 2;
	return 0;
}
