// SPDX-License-Identifier: Apache-2.0
/*
 * fpx command line tool.
 *
 *   fpx info                    sensor and firmware information
 *   fpx list                    template ids stored on the sensor
 *   fpx identify [-t ms]        wait for a finger and print the matching id
 *   fpx bind [-t ms] <user>     bind the touched finger to a local user
 *   fpx unbind <user>           forget a user's bindings (host side only)
 *   fpx bindings <user>         show a user's bindings
 */
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fpx/bindings.h"
#include "fpx/fpx.h"

static struct fpx_device *volatile active_dev;

static void on_sigint(int sig) {
	(void)sig;
	if (active_dev)
		fpx_cancel(active_dev);
}

static void usage(FILE *f) {
	fputs("usage: fpx <command> [options]\n"
	      "\n"
	      "commands:\n"
	      "  info              show sensor and firmware information\n"
	      "  list              list template ids stored on the sensor\n"
	      "  identify [-t ms]  wait for a finger and show the matching id\n"
	      "  bind [-t ms] USER bind the touched finger to USER\n"
	      "  unbind USER       forget USER's bindings (sensor untouched)\n"
	      "  bindings USER     show the template ids bound to USER\n"
	      "  help              show this help\n"
	      "\n"
	      "environment:\n"
	      "  " FPX_ENV_TRACE "=1   hex dump every USB packet\n"
	      "  " FPX_ENV_DEBUG "=1   verbose log messages\n",
	      f);
}

static void print_id(const struct fpx_template_id *id) {
	for (size_t i = 0; i < FPX_TEMPLATE_ID_LEN; i++)
		printf("%02x", id->bytes[i]);
	fputs("  \"", stdout);
	for (size_t i = 0; i < FPX_TEMPLATE_ID_LEN; i++) {
		uint8_t c = id->bytes[i];

		putchar(c >= 0x20 && c <= 0x7e ? c : '.');
	}
	fputs("\"\n", stdout);
}

static int report(int err) {
	fflush(stdout);
	if (err == FPX_E_PERM && geteuid() != 0)
		fprintf(stderr, "fpx: %s (try: sudo fpx ...)\n",
			fpx_strerror(err));
	else
		fprintf(stderr, "fpx: %s\n", fpx_strerror(err));
	return 1;
}

static int open_device(struct fpx_device **dev) {
	int err = fpx_device_open_first(dev);

	if (err)
		return err;
	active_dev = *dev;
	return FPX_OK;
}

static void close_device(struct fpx_device *dev) {
	active_dev = NULL;
	fpx_device_close(dev);
}

static int cmd_info(void) {
	struct fpx_usb_info info[4];
	struct fpx_device *dev;
	size_t n = 0;
	int err;

	err = fpx_scan(info, 4, &n);
	if (err)
		return report(err);
	if (n == 0)
		return report(FPX_E_NODEV);

	for (size_t i = 0; i < n; i++)
		printf("sensor %zu: %04x:%04x bus %03u dev %03u  %s\n"
		       "  driver:   %s\n"
		       "  serial:   %s\n",
		       i, info[i].vid, info[i].pid, info[i].busnum,
		       info[i].devnum, info[i].product, info[i].driver_name,
		       info[i].serial[0] ? info[i].serial : "(none)");

	err = open_device(&dev);
	if (err)
		return report(err);
	printf("  firmware: %s\n", fpx_device_fw_version(dev));
	close_device(dev);
	return 0;
}

static int cmd_list(void) {
	struct fpx_template_id ids[FPX_MAX_TEMPLATES];
	struct fpx_device *dev;
	size_t n = 0;
	int err;

	err = open_device(&dev);
	if (err)
		return report(err);

	err = fpx_list(dev, ids, FPX_MAX_TEMPLATES, &n);
	close_device(dev);
	if (err)
		return report(err);

	printf("%zu template(s) stored on the sensor (max %u)\n", n,
	       FPX_MAX_TEMPLATES);
	for (size_t i = 0; i < n; i++) {
		printf("  [%zu] ", i);
		print_id(&ids[i]);
	}
	return 0;
}

/* Parse "-t ms"; returns 0 on success, 2 on a usage error. */
static int parse_timeout(int argc, char **argv, uint32_t *timeout_ms) {
	int opt;

	*timeout_ms = FPX_FINGER_WAIT_TIMEOUT_MS;
	while ((opt = getopt(argc, argv, "t:")) != -1) {
		char *end;
		unsigned long v;

		if (opt != 't') {
			usage(stderr);
			return 2;
		}
		v = strtoul(optarg, &end, 10);
		if (*end || v == 0 || v > UINT32_MAX) {
			fprintf(stderr, "fpx: invalid timeout '%s'\n", optarg);
			return 2;
		}
		*timeout_ms = (uint32_t)v;
	}
	return 0;
}

/* Open the sensor, wait for one finger and identify it. */
static int identify_once(uint32_t timeout_ms, struct fpx_template_id *match) {
	struct fpx_device *dev;
	int err;

	err = open_device(&dev);
	if (err)
		return err;

	printf("Touch the sensor (%u s, Ctrl-C to cancel)...\n",
	       timeout_ms / 1000u);
	fflush(stdout);

	err = fpx_identify(dev, timeout_ms, match);
	close_device(dev);
	return err;
}

static int cmd_identify(int argc, char **argv) {
	struct fpx_template_id match;
	uint32_t timeout_ms;
	int err;

	if (parse_timeout(argc, argv, &timeout_ms))
		return 2;

	err = identify_once(timeout_ms, &match);
	if (err == FPX_E_NOMATCH) {
		puts("NO MATCH");
		return 1;
	}
	if (err)
		return report(err);

	fputs("MATCH ", stdout);
	print_id(&match);
	return 0;
}

/* Validate the user argument: safe file name and an existing account. */
static const char *user_arg(int argc, char **argv) {
	const char *user;

	if (optind >= argc) {
		usage(stderr);
		return NULL;
	}
	user = argv[optind];
	if (!fpx_bindings_valid_user(user) || !getpwnam(user)) {
		fprintf(stderr, "fpx: no such user '%s'\n", user);
		return NULL;
	}
	return user;
}

static int cmd_bind(int argc, char **argv) {
	struct fpx_template_id match;
	uint32_t timeout_ms;
	const char *user;
	int err;

	if (parse_timeout(argc, argv, &timeout_ms))
		return 2;
	user = user_arg(argc, argv);
	if (!user)
		return 2;

	err = identify_once(timeout_ms, &match);
	if (err == FPX_E_NOMATCH) {
		fputs("fpx: finger not enrolled on the sensor; enroll it in "
		      "Windows Hello first\n",
		      stderr);
		return 1;
	}
	if (err)
		return report(err);

	err = fpx_bindings_add(FPX_STATE_DIR, user, &match);
	if (err)
		return report(err);

	printf("bound to %s: ", user);
	print_id(&match);
	return 0;
}

static int cmd_unbind(int argc, char **argv) {
	const char *user;
	int err;

	optind = 1;
	user = user_arg(argc, argv);
	if (!user)
		return 2;

	/* Host side only: the template stays on the sensor. */
	err = fpx_bindings_clear(FPX_STATE_DIR, user);
	if (err)
		return report(err);
	printf("removed all bindings of %s (sensor untouched)\n", user);
	return 0;
}

static int cmd_bindings(int argc, char **argv) {
	struct fpx_template_id ids[FPX_MAX_TEMPLATES];
	const char *user;
	size_t n = 0;
	int err;

	optind = 1;
	user = user_arg(argc, argv);
	if (!user)
		return 2;

	err = fpx_bindings_load(FPX_STATE_DIR, user, ids, FPX_MAX_TEMPLATES,
				&n);
	if (err)
		return report(err);

	printf("%zu binding(s) for %s\n", n, user);
	for (size_t i = 0; i < n; i++) {
		printf("  [%zu] ", i);
		print_id(&ids[i]);
	}
	return 0;
}

int main(int argc, char **argv) {
	struct sigaction sa = { .sa_handler = on_sigint };
	const char *cmd;

	if (argc < 2) {
		usage(stderr);
		return 2;
	}

	/* No SA_RESTART: let the finger wait see EINTR and check for cancel. */
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	cmd = argv[1];
	if (strcmp(cmd, "info") == 0)
		return cmd_info();
	if (strcmp(cmd, "list") == 0)
		return cmd_list();
	if (strcmp(cmd, "identify") == 0)
		return cmd_identify(argc - 1, argv + 1);
	if (strcmp(cmd, "bind") == 0)
		return cmd_bind(argc - 1, argv + 1);
	if (strcmp(cmd, "unbind") == 0)
		return cmd_unbind(argc - 1, argv + 1);
	if (strcmp(cmd, "bindings") == 0)
		return cmd_bindings(argc - 1, argv + 1);
	if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 ||
	    strcmp(cmd, "--help") == 0) {
		usage(stdout);
		return 0;
	}

	fprintf(stderr, "fpx: unknown command '%s'\n", cmd);
	usage(stderr);
	return 2;
}
