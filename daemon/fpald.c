// SPDX-License-Identifier: Apache-2.0
/*
 * fpald: announces the fingerprint sensor to GNOME as net.reactivated.Fprint
 * so the login and lock screens offer fingerprint. It never opens the
 * sensor; verification runs through the gdm-fingerprint PAM service.
 *
 * Started by D-Bus activation, exits after FPAL_FPALD_IDLE_MS of silence.
 */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "core/log.h"
#include "dbus/conn.h"
#include "fpal/fpal.h"
#include "fprint_service.h"

static volatile sig_atomic_t stop;

static void on_signal(int sig) {
	(void)sig;
	stop = 1;
}

static bool sensor_present(void *ud) {
	struct fpal_usb_info info[4];
	size_t n = 0;

	(void)ud;
	return fpal_scan(info, 4, &n) == FPAL_OK && n > 0;
}

static void usage(void) {
	fputs("usage: fpald [-d] [-s bus-socket]\n"
	      "  -d  verbose log messages\n"
	      "  -s  bus socket path (default " FPAL_DBUS_SYSTEM_SOCKET ")\n",
	      stderr);
}

/* Handle every complete message that has arrived. */
static int pump(struct fpal_dbus *c, const struct fpal_fprint_ops *ops) {
	struct fpal_dmsg m;
	int rc;

	while ((rc = fpal_dbus_recv(c, 0, &m)) == FPAL_OK) {
		if (m.type == FPAL_DMSG_SIGNAL && m.member &&
		    !strcmp(m.member, "NameLost")) {
			fpal_warn("lost the bus name, exiting");
			return FPAL_E_BUSY;
		}
		if (m.type == FPAL_DMSG_CALL)
			(void)fpal_fprint_handle(c, &m, ops);
	}
	return rc == FPAL_E_TIMEOUT ? FPAL_OK : rc;
}

int main(int argc, char **argv) {
	const struct fpal_fprint_ops ops = { .sensor_present = sensor_present };
	const char *sock = NULL;
	struct sigaction sa = { .sa_handler = on_signal };
	struct fpal_dbus c;
	uint32_t owner = 0;
	int opt, rc;

	while ((opt = getopt(argc, argv, "ds:h")) != -1) {
		switch (opt) {
		case 'd':
			fpal_log_set_debug(true);
			break;
		case 's':
			sock = optarg;
			break;
		default:
			usage();
			return opt == 'h' ? 0 : 2;
		}
	}

	/* no SA_RESTART: a signal must interrupt poll() */
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	rc = fpal_dbus_open(&c, sock);
	if (rc) {
		fpal_err("cannot connect to the bus: %s", fpal_strerror(rc));
		return 1;
	}
	rc = fpal_dbus_request_name(&c, FPAL_FPRINT_BUS_NAME,
				    FPAL_DBUS_NAME_FLAG_DO_NOT_QUEUE, &owner);
	if (rc || owner != FPAL_DBUS_REPLY_PRIMARY_OWNER) {
		fpal_err("cannot own %s (is fprintd running?)",
			 FPAL_FPRINT_BUS_NAME);
		return 1;
	}
	fpal_info("serving %s as %s", FPAL_FPRINT_BUS_NAME, c.unique);

	while (!stop) {
		struct pollfd pfd = { .fd = c.fd, .events = POLLIN };

		rc = poll(&pfd, 1, (int)FPAL_FPALD_IDLE_MS);
		if (rc < 0 && errno == EINTR)
			continue;
		if (rc < 0)
			break;
		if (rc == 0) {
			fpal_dbg("idle, exiting");
			break;
		}
		if (pump(&c, &ops) != FPAL_OK) {
			fpal_warn("bus connection ended");
			break;
		}
	}
	fpal_dbus_close(&c);
	return 0;
}
