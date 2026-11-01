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
#include "ui.h"

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

static struct fpal_ui ui;

/* "  ✓  Label           text": a mark in its colour, a bold label, the text. */
static void line(enum fpal_mark m, const char *label, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

static void line(enum fpal_mark m, const char *label, const char *fmt, ...) {
	va_list ap;

	printf("  %s", fpal_ui_style(&ui, fpal_ui_mark_style(m)));
	fpal_ui_pad(stdout, fpal_ui_mark(&ui, m), 2);
	printf("%s  %s", fpal_ui_style(&ui, UI_RESET),
	       fpal_ui_style(&ui, UI_BOLD));
	fpal_ui_pad(stdout, label, 16);
	printf("%s", fpal_ui_style(&ui, UI_RESET));
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
}

/* What to do about the line above, in yellow. */
static void hint(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void hint(const char *fmt, ...) {
	va_list ap;

	printf("%*s%s%s ", 6, "", fpal_ui_style(&ui, UI_YELLOW),
	       ui.utf8 ? "→" : "->");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("%s\n", fpal_ui_style(&ui, UI_RESET));
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
		line(UI_BAD, "Fingerprints", "cannot read the sensor: %s",
		     fpal_strerror(err));
		(*problems)++;
		return;
	}
	err = fpal_list(dev, ids, FPAL_MAX_TEMPLATES, &n);
	fpal_device_close(dev);
	if (err) {
		line(UI_BAD, "Fingerprints", "cannot list them: %s",
		     fpal_strerror(err));
		(*problems)++;
		return;
	}
	line(n ? UI_OK : UI_BAD, "Fingerprints",
	     "%zu on the sensor (it holds up to %u)", n, FPAL_MAX_TEMPLATES);
	for (size_t i = 0; i < n; i++) {
		const bool ours = fpal_bindings_contains(FPAL_STATE_DIR, user, &ids[i]);

		label[0] = '\0';
		(void)fpal_labels_get(FPAL_STATE_DIR, &ids[i], label, sizeof(label));
		printf("%*s%s[%zu]%s  ", 22, "", fpal_ui_style(&ui, UI_DIM), i,
		       fpal_ui_style(&ui, UI_RESET));
		fpal_ui_pad(stdout, label[0] ? label : "(no name)",
			    FPAL_LABEL_MAX / 2);
		if (ours)
			printf(" %s%s %s%s", fpal_ui_style(&ui, UI_GREEN),
			       ui.utf8 ? "←" : "<-", user,
			       fpal_ui_style(&ui, UI_RESET));
		putchar('\n');
		bound += ours;
	}
	if (bound) {
		line(UI_OK, "Bound to you", "%s: %zu finger(s)", user, bound);
	} else {
		line(UI_BAD, "Bound to you", "%s has no finger bound", user);
		hint("run: sudo fpal bind %s", user);
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
		user = fpal_ui_invoking_user();
	}
	ui = fpal_ui_init(stdout);

	printf("\n  %sfpal status%s\n\n", fpal_ui_style(&ui, UI_BOLD),
	       fpal_ui_style(&ui, UI_RESET));

	err = fpal_scan(info, 4, &n);
	if (err || n == 0) {
		line(UI_BAD, "Sensor", "none found");
		hint("plug in a supported fingerprint sensor");
		problems++;
	} else {
		line(UI_OK, "Sensor", "%04x:%04x  %s  (driver %s)", info[0].vid,
		     info[0].pid, info[0].product, info[0].driver_name);
	}

	if (geteuid() == 0 && n > 0)
		show_fingers(user, &problems);
	else if (n > 0)
		line(UI_INFO, "Fingerprints", "run with sudo to read the sensor");

	for (size_t i = 0; i < sizeof(pam_files) / sizeof(pam_files[0]); i++) {
		if (fpal_file_has_module(pam_files[i], MODULE)) {
			if (used[0])
				strncat(used, ", ", sizeof(used) - strlen(used) - 1);
			strncat(used, strrchr(pam_files[i], '/') + 1,
				sizeof(used) - strlen(used) - 1);
		}
	}
	if (used[0]) {
		line(UI_OK, "sudo and login", "pam_fpal.so is active (%s)", used);
	} else {
		line(UI_BAD, "sudo and login", "they will not ask for a fingerprint");
		hint("run: sudo fpal setup");
		problems++;
	}

	err = fprint_owner(who, sizeof(who));
	if (err == FPAL_OK && !strcmp(who, "fpald")) {
		line(UI_OK, "GNOME screens", "fpald announces the sensor");
	} else if (err == FPAL_OK) {
		line(UI_BAD, "GNOME screens", "%s owns %s, not fpald", who,
		     FPRINT_NAME);
		hint("run: sudo fpal setup");
		problems++;
	} else if (err == FPAL_E_NODATA) {
		line(UI_BAD, "GNOME screens",
		     "nobody answers for %s, so GNOME will not offer a fingerprint",
		     FPRINT_NAME);
		hint("run: sudo fpal setup");
		problems++;
	} else {
		line(UI_INFO, "GNOME screens", "cannot ask the system bus: %s",
		     fpal_strerror(err));
	}
	if (fprintd_masked())
		line(UI_OK, "fprintd", "masked, so fpal can use the sensor");
	else
		line(UI_INFO, "fprintd",
		     "not masked (fine while fpald is what GNOME talks to)");

	if (problems)
		printf("\n  %s%sNot ready:%s %u thing(s) to fix, see the arrows above\n\n",
		       fpal_ui_style(&ui, UI_RED), fpal_ui_style(&ui, UI_BOLD),
		       fpal_ui_style(&ui, UI_RESET), problems);
	else
		printf("\n  %s%sReady:%s fingerprint login is set up\n\n",
		       fpal_ui_style(&ui, UI_GREEN), fpal_ui_style(&ui, UI_BOLD),
		       fpal_ui_style(&ui, UI_RESET));
	return problems ? 1 : 0;
}
