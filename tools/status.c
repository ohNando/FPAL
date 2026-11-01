// SPDX-License-Identifier: Apache-2.0
#include "status.h"

#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dbus/conn.h"
#include "dbus/marshal.h"
#include "fpal/bindings.h"
#include "fpal/fpal.h"
#include "fpal/labels.h"

#define FPRINT_NAME "net.reactivated.Fprint"
#define MODULE "pam_fpal.so"

bool fpal_file_has_module(const char *path, const char *module) {
	char line[512];
	bool found = false;
	FILE *f = fopen(path, "r");

	if (!f)
		return false;
	while (!found && fgets(line, sizeof(line), f)) {
		const char *p = line;

		while (*p == ' ' || *p == '\t')
			p++;
		if (*p != '#' && strstr(p, module))
			found = true;
	}
	fclose(f);
	return found;
}

/* One line of the report: a fixed width label, then the text. */
static void row(const char *label, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static void row(const char *label, const char *fmt, ...) {
	va_list ap;

	printf("%-14s", label);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
}

/* Ask the bus daemon a question about a name; the answer lands in @reply. */
static int bus_ask(struct fpal_dbus *c, const char *member, const char *name,
		   struct fpal_dmsg *reply) {
	uint8_t body[96];
	struct fpal_dw w;
	struct fpal_dmsg call = { .type = FPAL_DMSG_CALL,
				  .path = "/org/freedesktop/DBus",
				  .iface = "org.freedesktop.DBus",
				  .member = member,
				  .dest = "org.freedesktop.DBus",
				  .sig = "s" };
	int err;

	fpal_dw_init(&w, body, sizeof(body));
	fpal_dw_str(&w, name);
	if (w.err)
		return w.err;
	call.body = body;
	call.body_len = (uint32_t)w.len;
	err = fpal_dbus_call(c, &call, FPAL_DBUS_TIMEOUT_MS, reply);
	if (!err && reply->type != FPAL_DMSG_RETURN)
		return FPAL_E_NODATA;
	return err;
}

/*
 * Who owns net.reactivated.Fprint? FPAL_OK with @who filled ("fpald",
 * "fprintd", ...), FPAL_E_NODATA if nobody does, another error if the system
 * bus cannot be asked.
 */
static int fprint_owner(char *who, size_t cap) {
	struct fpal_dbus c;
	struct fpal_dmsg r;
	struct fpal_dr d;
	char path[64], comm[64] = "";
	uint32_t pid;
	FILE *f;
	int err;

	err = fpal_dbus_open(&c, NULL);
	if (err)
		return err;
	err = bus_ask(&c, "NameHasOwner", FPRINT_NAME, &r);
	if (!err) {
		fpal_dr_init(&d, r.body, r.body_len);
		if (!fpal_dr_u32(&d))
			err = FPAL_E_NODATA;
	}
	if (!err)
		err = bus_ask(&c, "GetConnectionUnixProcessID", FPRINT_NAME, &r);
	if (!err) {
		fpal_dr_init(&d, r.body, r.body_len);
		pid = fpal_dr_u32(&d);
		snprintf(path, sizeof(path), "/proc/%u/comm", pid);
		f = fopen(path, "r");
		if (f) {
			if (fgets(comm, sizeof(comm), f))
				comm[strcspn(comm, "\n")] = '\0';
			fclose(f);
		}
		snprintf(who, cap, "%s", comm[0] ? comm : "an unknown process");
	}
	fpal_dbus_close(&c);
	return err;
}

static bool fprintd_masked(void) {
	char target[64];
	ssize_t n = readlink("/etc/systemd/system/fprintd.service", target,
			     sizeof(target) - 1);

	if (n < 0)
		return false;
	target[n] = '\0';
	return !strcmp(target, "/dev/null");
}

/* The fingerprints on the sensor and the user's bindings; needs root. */
static void show_fingers(const char *user, unsigned *problems) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	char label[FPAL_LABEL_MAX + 1];
	struct fpal_device *dev;
	size_t n = 0, bound = 0;
	int err;

	err = fpal_device_open_first(&dev);
	if (err) {
		row("fingerprints", "cannot read the sensor: %s", fpal_strerror(err));
		return;
	}
	err = fpal_list(dev, ids, FPAL_MAX_TEMPLATES, &n);
	fpal_device_close(dev);
	if (err) {
		row("fingerprints", "cannot list them: %s", fpal_strerror(err));
		return;
	}
	row("fingerprints", "%zu on the sensor (it holds up to %u)", n,
	    FPAL_MAX_TEMPLATES);
	for (size_t i = 0; i < n; i++) {
		const bool ours = fpal_bindings_contains(FPAL_STATE_DIR, user, &ids[i]);

		label[0] = '\0';
		(void)fpal_labels_get(FPAL_STATE_DIR, &ids[i], label, sizeof(label));
		printf("%14s[%zu] %-*s %s\n", "", i, (int)FPAL_LABEL_MAX,
		       label[0] ? label : "(no name)",
		       ours ? "<- bound to you" : "");
		bound += ours;
	}
	row("bound to", "%s: %zu finger(s)", user, bound);
	if (!bound) {
		puts("              ! no finger is bound to this user");
		(*problems)++;
	}
}

int fpal_cmd_status(int argc, char **argv) {
	static const char *const pam_files[] = {
		"/etc/pam.d/system-auth", "/etc/pam.d/fingerprint-auth",
		"/etc/pam.d/common-auth", "/etc/pam.d/sudo",
	};
	struct fpal_usb_info info[4];
	char who[64], used[256] = "";
	const char *user = NULL;
	unsigned problems = 0;
	size_t n = 0;
	int err;

	if (argc > 2) {
		fputs("usage: fpal status [USER]\n", stderr);
		return 2;
	}
	if (argc == 2) {
		user = argv[1];
		if (!fpal_bindings_valid_user(user) || !getpwnam(user)) {
			fprintf(stderr, "fpal: no such user '%s'\n", user);
			return 2;
		}
	} else {
		/* under sudo the interesting user is the one who typed it */
		const char *sudo_user = getenv("SUDO_USER");
		struct passwd *pw = getpwuid(getuid());

		user = sudo_user && getpwnam(sudo_user) ? sudo_user :
			pw ? pw->pw_name : "root";
	}

	err = fpal_scan(info, 4, &n);
	if (err || n == 0) {
		row("sensor", "none found");
		puts("              ! no supported fingerprint sensor is plugged in");
		problems++;
	} else {
		row("sensor", "%04x:%04x %s (driver %s)", info[0].vid,
		    info[0].pid, info[0].product, info[0].driver_name);
	}

	if (geteuid() == 0 && n > 0)
		show_fingers(user, &problems);
	else if (n > 0)
		row("fingerprints", "run with sudo to read the sensor and the bindings");

	for (size_t i = 0; i < sizeof(pam_files) / sizeof(pam_files[0]); i++) {
		if (fpal_file_has_module(pam_files[i], MODULE)) {
			if (used[0])
				strncat(used, ", ", sizeof(used) - strlen(used) - 1);
			strncat(used, strrchr(pam_files[i], '/') + 1,
				sizeof(used) - strlen(used) - 1);
		}
	}
	if (used[0]) {
		row("sudo, login", "pam_fpal.so is active (%s)", used);
	} else {
		row("sudo, login", "not set up: they will not ask for a fingerprint");
		puts("              ! run: sudo fpal setup");
		problems++;
	}

	err = fprint_owner(who, sizeof(who));
	if (err == FPAL_OK && !strcmp(who, "fpald")) {
		row("GNOME screens", "fpald announces the sensor");
	} else if (err == FPAL_OK) {
		row("GNOME screens", "%s owns %s, not fpald", who, FPRINT_NAME);
		puts("              ! run: sudo fpal setup");
		problems++;
	} else if (err == FPAL_E_NODATA) {
		row("GNOME screens", "nobody answers for %s: the lock and "
				     "login screens will not offer a fingerprint",
		    FPRINT_NAME);
		puts("              ! run: sudo fpal setup");
		problems++;
	} else {
		row("GNOME screens", "cannot ask the system bus: %s",
		    fpal_strerror(err));
	}
	row("fprintd", "%s", fprintd_masked() ? "masked (good: fpal uses the sensor)" :
						"not masked");

	puts(problems ? "\nnot ready, see the lines starting with !" :
			"\nready: fingerprint login is set up");
	return problems ? 1 : 0;
}
