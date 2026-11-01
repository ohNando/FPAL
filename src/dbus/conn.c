// SPDX-License-Identifier: Apache-2.0
#include "dbus/conn.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "core/log.h"
#include "dbus/marshal.h"

#define BUS_NAME "org.freedesktop.DBus"
#define BUS_PATH "/org/freedesktop/DBus"
#define LINE_MAX_LEN 256u

static uint64_t now_ms(void) {
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int send_all(int fd, const void *buf, size_t len) {
	const uint8_t *p = buf;

	while (len) {
		ssize_t n = send(fd, p, len, MSG_NOSIGNAL);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return FPAL_E_IO;
		}
		p += n;
		len -= (size_t)n;
	}
	return FPAL_OK;
}

/*
 * Wait for the socket to become readable and append what arrives to the rx
 * buffer. FPAL_E_TIMEOUT if nothing came within @timeout_ms.
 */
static int fill(struct fpal_dbus *c, uint32_t timeout_ms) {
	struct pollfd pfd = { .fd = c->fd, .events = POLLIN };
	int rc;
	ssize_t n;

	if (c->rx_len == sizeof(c->rx))
		return FPAL_E_OVERFLOW;
	do {
		rc = poll(&pfd, 1, (int)timeout_ms);
	} while (rc < 0 && errno == EINTR);
	if (rc < 0)
		return FPAL_E_IO;
	if (rc == 0)
		return FPAL_E_TIMEOUT;

	do {
		n = recv(c->fd, c->rx + c->rx_len, sizeof(c->rx) - c->rx_len, 0);
	} while (n < 0 && errno == EINTR);
	if (n <= 0)
		return FPAL_E_IO; /* error or the bus closed the connection */
	c->rx_len += (size_t)n;
	return FPAL_OK;
}

static void rx_drop(struct fpal_dbus *c, size_t n) {
	memmove(c->rx, c->rx + n, c->rx_len - n);
	c->rx_len -= n;
}

/* Read one "\r\n" terminated line of the text handshake. */
static int read_line(struct fpal_dbus *c, char *line, size_t cap) {
	for (;;) {
		uint8_t *eol = memchr(c->rx, '\n', c->rx_len);

		if (eol) {
			size_t n = (size_t)(eol - c->rx) + 1;

			if (n >= cap)
				return FPAL_E_PROTO;
			memcpy(line, c->rx, n);
			line[n] = '\0';
			rx_drop(c, n);
			return FPAL_OK;
		}
		if (c->rx_len >= LINE_MAX_LEN)
			return FPAL_E_PROTO;
		int rc = fill(c, FPAL_DBUS_TIMEOUT_MS);

		if (rc)
			return rc;
	}
}

/* "AUTH EXTERNAL <hex of the decimal uid>": the bus checks it against the
 * credentials of the socket. */
static int sasl_external(struct fpal_dbus *c) {
	char dec[16], cmd[64], line[LINE_MAX_LEN + 1];
	size_t n = 0;
	int rc;

	snprintf(dec, sizeof(dec), "%u", (unsigned)geteuid());
	cmd[n++] = '\0'; /* the single credentials byte every connection starts with */
	n += (size_t)snprintf(cmd + n, sizeof(cmd) - n, "AUTH EXTERNAL ");
	for (const char *d = dec; *d; d++)
		n += (size_t)snprintf(cmd + n, sizeof(cmd) - n, "%02x", *d);
	n += (size_t)snprintf(cmd + n, sizeof(cmd) - n, "\r\n");

	rc = send_all(c->fd, cmd, n);
	if (rc)
		return rc;
	rc = read_line(c, line, sizeof(line));
	if (rc)
		return rc;
	if (strncmp(line, "OK ", 3) != 0) {
		fpal_err("dbus: authentication refused: %.40s", line);
		return FPAL_E_PERM;
	}
	return send_all(c->fd, "BEGIN\r\n", 7);
}

uint32_t fpal_dbus_serial(struct fpal_dbus *c) {
	if (++c->next_serial == 0)
		c->next_serial = 1;
	return c->next_serial;
}

int fpal_dbus_send(struct fpal_dbus *c, const struct fpal_dmsg *m) {
	uint8_t buf[FPAL_DBUS_MSG_MAX];
	struct fpal_dmsg copy = *m;
	size_t n;
	int rc;

	if (!copy.serial)
		copy.serial = fpal_dbus_serial(c);
	rc = fpal_dmsg_build(buf, sizeof(buf), &n, &copy);
	return rc ? rc : send_all(c->fd, buf, n);
}

int fpal_dbus_recv(struct fpal_dbus *c, uint32_t timeout_ms,
		   struct fpal_dmsg *m) {
	uint64_t deadline = now_ms() + timeout_ms;

	if (c->rx_handed) {
		rx_drop(c, c->rx_handed);
		c->rx_handed = 0;
	}
	for (;;) {
		uint64_t now;
		uint32_t left;
		int rc;

		if (c->rx_len >= FPAL_DMSG_FIXED) {
			size_t total;

			rc = fpal_dmsg_total_len(c->rx, &total);

			if (rc)
				return rc;
			if (c->rx_len >= total) {
				rc = fpal_dmsg_parse(c->rx, total, m);
				if (rc)
					return rc;
				c->rx_handed = total;
				return FPAL_OK;
			}
		}
		now = now_ms();
		left = now >= deadline ? 0 : (uint32_t)(deadline - now);
		rc = fill(c, left);
		if (rc)
			return rc;
	}
}

int fpal_dbus_call(struct fpal_dbus *c, const struct fpal_dmsg *call,
		   uint32_t timeout_ms, struct fpal_dmsg *reply) {
	struct fpal_dmsg copy = *call;
	uint64_t deadline = now_ms() + timeout_ms;
	int rc;

	if (!copy.serial)
		copy.serial = fpal_dbus_serial(c);
	rc = fpal_dbus_send(c, &copy);
	if (rc)
		return rc;
	for (;;) {
		uint64_t now = now_ms();

		if (now >= deadline)
			return FPAL_E_TIMEOUT;
		rc = fpal_dbus_recv(c, (uint32_t)(deadline - now), reply);
		if (rc)
			return rc;
		if ((reply->type == FPAL_DMSG_RETURN ||
		     reply->type == FPAL_DMSG_ERROR) &&
		    reply->reply_serial == copy.serial)
			return FPAL_OK;
		/* NameAcquired and other signals are of no interest here */
	}
}

int fpal_dbus_reply(struct fpal_dbus *c, const struct fpal_dmsg *call,
		    const char *sig, const uint8_t *body, size_t body_len) {
	struct fpal_dmsg r = { .type = FPAL_DMSG_RETURN,
			       .reply_serial = call->serial,
			       .dest = call->sender,
			       .sig = sig,
			       .body = body,
			       .body_len = (uint32_t)body_len };

	if (call->flags & FPAL_DMSG_NO_REPLY)
		return FPAL_OK;
	return fpal_dbus_send(c, &r);
}

int fpal_dbus_reply_error(struct fpal_dbus *c, const struct fpal_dmsg *call,
			  const char *errname, const char *text) {
	uint8_t body[256];
	struct fpal_dw w;
	struct fpal_dmsg r = { .type = FPAL_DMSG_ERROR,
			       .reply_serial = call->serial,
			       .dest = call->sender,
			       .errname = errname };

	if (call->flags & FPAL_DMSG_NO_REPLY)
		return FPAL_OK;
	if (text) {
		fpal_dw_init(&w, body, sizeof(body));
		fpal_dw_str(&w, text);
		if (!w.err) {
			r.sig = "s";
			r.body = body;
			r.body_len = (uint32_t)w.len;
		}
	}
	return fpal_dbus_send(c, &r);
}

/* ---- bus daemon calls ------------------------------------------------- */

static int bus_call(struct fpal_dbus *c, const char *member,
		    const char *sig, const uint8_t *body, size_t len,
		    struct fpal_dmsg *reply) {
	struct fpal_dmsg call = { .type = FPAL_DMSG_CALL,
				  .path = BUS_PATH,
				  .iface = BUS_NAME,
				  .member = member,
				  .dest = BUS_NAME,
				  .sig = sig,
				  .body = body,
				  .body_len = (uint32_t)len };
	int rc = fpal_dbus_call(c, &call, FPAL_DBUS_TIMEOUT_MS, reply);

	if (rc)
		return rc;
	if (reply->type == FPAL_DMSG_ERROR) {
		fpal_warn("dbus: %s failed: %s", member, reply->errname);
		return !strcmp(reply->errname,
			       "org.freedesktop.DBus.Error.AccessDenied")
			       ? FPAL_E_PERM
			       : FPAL_E_PROTO;
	}
	return FPAL_OK;
}

static int hello(struct fpal_dbus *c) {
	struct fpal_dmsg reply;
	struct fpal_dr r;
	const char *name;
	int rc = bus_call(c, "Hello", NULL, NULL, 0, &reply);

	if (rc)
		return rc;
	if (!reply.sig || strcmp(reply.sig, "s"))
		return FPAL_E_PROTO;
	fpal_dr_init(&r, reply.body, reply.body_len);
	name = fpal_dr_str(&r);
	if (!name || strlen(name) >= sizeof(c->unique))
		return FPAL_E_PROTO;
	memcpy(c->unique, name, strlen(name) + 1);
	return FPAL_OK;
}

int fpal_dbus_request_name(struct fpal_dbus *c, const char *name,
			   uint32_t flags, uint32_t *result) {
	uint8_t body[FPAL_DBUS_NAME_MAX + 16];
	struct fpal_dmsg reply;
	struct fpal_dw w;
	struct fpal_dr r;
	int rc;

	fpal_dw_init(&w, body, sizeof(body));
	fpal_dw_str(&w, name);
	fpal_dw_u32(&w, flags);
	if (w.err)
		return w.err;
	rc = bus_call(c, "RequestName", "su", body, w.len, &reply);
	if (rc)
		return rc;
	if (!reply.sig || strcmp(reply.sig, "u"))
		return FPAL_E_PROTO;
	fpal_dr_init(&r, reply.body, reply.body_len);
	*result = fpal_dr_u32(&r);
	return r.err;
}

/* ---- open / close ------------------------------------------------------ */

int fpal_dbus_attach(struct fpal_dbus *c, int fd) {
	int rc;

	memset(c, 0, sizeof(*c));
	c->fd = fd;
	rc = sasl_external(c);
	if (!rc)
		rc = hello(c);
	if (rc)
		fpal_dbus_close(c);
	return rc;
}

int fpal_dbus_open(struct fpal_dbus *c, const char *path) {
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	int fd;

	if (!path)
		path = FPAL_DBUS_SYSTEM_SOCKET;
	if (strlen(path) >= sizeof(sa.sun_path))
		return FPAL_E_INVAL;
	strcpy(sa.sun_path, path);

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return FPAL_E_IO;
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		int e = errno;

		close(fd);
		return e == EACCES ? FPAL_E_PERM : FPAL_E_NODEV;
	}
	return fpal_dbus_attach(c, fd);
}

void fpal_dbus_close(struct fpal_dbus *c) {
	if (c->fd >= 0)
		close(c->fd);
	c->fd = -1;
}
