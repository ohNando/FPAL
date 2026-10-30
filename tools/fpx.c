// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * fpx command line tool.
 *
 *   fpx info                    sensor and firmware information
 *   fpx list                    template ids stored on the sensor
 *   fpx identify [-t ms]        wait for a finger and print the matching id
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fpx/fpx.h"

static struct fpx_device *volatile active_dev;

static void on_sigint(int sig)
{
	(void)sig;
	if (active_dev)
		fpx_cancel(active_dev);
}

static void usage(FILE *f)
{
	fputs("usage: fpx <command> [options]\n"
	      "\n"
	      "commands:\n"
	      "  info              show sensor and firmware information\n"
	      "  list              list template ids stored on the sensor\n"
	      "  identify [-t ms]  wait for a finger and show the matching id\n"
	      "  help              show this help\n"
	      "\n"
	      "environment:\n"
	      "  " FPX_ENV_TRACE "=1   hex dump every USB packet\n"
	      "  " FPX_ENV_DEBUG "=1   verbose log messages\n",
	      f);
}

static void print_id(const struct fpx_template_id *id)
{
	for (size_t i = 0; i < FPX_TEMPLATE_ID_LEN; i++)
		printf("%02x", id->bytes[i]);
	fputs("  \"", stdout);
	for (size_t i = 0; i < FPX_TEMPLATE_ID_LEN; i++) {
		uint8_t c = id->bytes[i];

		putchar(c >= 0x20 && c <= 0x7e ? c : '.');
	}
	fputs("\"\n", stdout);
}

static int report(int err)
{
	fflush(stdout);
	if (err == FPX_E_PERM && geteuid() != 0)
		fprintf(stderr, "fpx: %s (try: sudo fpx ...)\n",
			fpx_strerror(err));
	else
		fprintf(stderr, "fpx: %s\n", fpx_strerror(err));
	return 1;
}

static int open_device(struct fpx_device **dev)
{
	int err = fpx_device_open_first(dev);

	if (err)
		return err;
	active_dev = *dev;
	return FPX_OK;
}

static void close_device(struct fpx_device *dev)
{
	active_dev = NULL;
	fpx_device_close(dev);
}

static int cmd_info(void)
{
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

static int cmd_list(void)
{
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

static int cmd_identify(int argc, char **argv)
{
	uint32_t timeout_ms = FPX_FINGER_WAIT_TIMEOUT_MS;
	struct fpx_template_id match;
	struct fpx_device *dev;
	int opt, err;

	while ((opt = getopt(argc, argv, "t:")) != -1) {
		switch (opt) {
		case 't': {
			char *end;
			unsigned long v = strtoul(optarg, &end, 10);

			if (*end || v == 0 || v > UINT32_MAX) {
				fprintf(stderr, "fpx: invalid timeout '%s'\n",
					optarg);
				return 2;
			}
			timeout_ms = (uint32_t)v;
			break;
		}
		default:
			usage(stderr);
			return 2;
		}
	}

	err = open_device(&dev);
	if (err)
		return report(err);

	printf("Touch the sensor (%u s, Ctrl-C to cancel)...\n",
	       timeout_ms / 1000u);
	fflush(stdout);

	err = fpx_identify(dev, timeout_ms, &match);
	close_device(dev);

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

int main(int argc, char **argv)
{
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
	if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 ||
	    strcmp(cmd, "--help") == 0) {
		usage(stdout);
		return 0;
	}

	fprintf(stderr, "fpx: unknown command '%s'\n", cmd);
	usage(stderr);
	return 2;
}
