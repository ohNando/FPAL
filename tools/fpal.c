// SPDX-License-Identifier: Apache-2.0
/*
 * fpal command line tool.
 *
 *   fpal info                    sensor and firmware information
 *   fpal list                    template ids stored on the sensor
 *   fpal identify [-t ms]        wait for a finger and print the matching id
 *   fpal bind [-t ms] <user>     bind the touched finger to a local user
 *   fpal unbind <user>           forget a user's bindings (host side only)
 *   fpal bindings <user>         show a user's bindings
 *   fpal enroll [--dry-run] [--allow-chip-write] <user>
 *                                store a NEW finger on the sensor (ENROLL=1 builds)
 */
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fpal/bindings.h"
#include "fpal/fpal.h"

static struct fpal_device *volatile active_dev;

static void on_sigint(int sig) {
	(void)sig;
	if (active_dev)
		fpal_cancel(active_dev);
}

static void usage(FILE *f) {
	fputs("usage: fpal <command> [options]\n"
	      "\n"
	      "commands:\n"
	      "  info              show sensor and firmware information\n"
	      "  list              list template ids stored on the sensor\n"
	      "  identify [-t ms]  wait for a finger and show the matching id\n"
	      "  bind [-t ms] USER bind the touched finger to USER\n"
	      "  unbind USER       forget USER's bindings (sensor untouched)\n"
	      "  bindings USER     show the template ids bound to USER\n"
#ifdef FPAL_ENABLE_ENROLL
	      "  enroll [--dry-run] [--allow-chip-write] USER\n"
	      "                    store a new finger on the sensor and bind it to USER\n"
#endif
	      "  version           show the fpal version\n"
	      "  help              show this help\n"
	      "\n"
	      "environment:\n"
	      "  " FPAL_ENV_TRACE "=1   hex dump every USB packet\n"
	      "  " FPAL_ENV_DEBUG "=1   verbose log messages\n",
	      f);
}

static void print_id(const struct fpal_template_id *id) {
	for (size_t i = 0; i < FPAL_TEMPLATE_ID_LEN; i++)
		printf("%02x", id->bytes[i]);
	fputs("  \"", stdout);
	for (size_t i = 0; i < FPAL_TEMPLATE_ID_LEN; i++) {
		uint8_t c = id->bytes[i];

		putchar(c >= 0x20 && c <= 0x7e ? c : '.');
	}
	fputs("\"\n", stdout);
}

static int report(int err) {
	fflush(stdout);
	if (err == FPAL_E_PERM && geteuid() != 0)
		fprintf(stderr, "fpal: %s (try: sudo fpal ...)\n",
			fpal_strerror(err));
	else
		fprintf(stderr, "fpal: %s\n", fpal_strerror(err));
	return 1;
}

static int open_device(struct fpal_device **dev) {
	int err = fpal_device_open_first(dev);

	if (err)
		return err;
	active_dev = *dev;
	return FPAL_OK;
}

static void close_device(struct fpal_device *dev) {
	active_dev = NULL;
	fpal_device_close(dev);
}

static int cmd_info(void) {
	struct fpal_usb_info info[4];
	struct fpal_device *dev;
	size_t n = 0;
	int err;

	err = fpal_scan(info, 4, &n);
	if (err)
		return report(err);
	if (n == 0)
		return report(FPAL_E_NODEV);

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
	printf("  firmware: %s\n", fpal_device_fw_version(dev));
	close_device(dev);
	return 0;
}

static int cmd_list(void) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	struct fpal_device *dev;
	size_t n = 0;
	int err;

	err = open_device(&dev);
	if (err)
		return report(err);

	err = fpal_list(dev, ids, FPAL_MAX_TEMPLATES, &n);
	close_device(dev);
	if (err)
		return report(err);

	printf("%zu template(s) stored on the sensor (max %u)\n", n,
	       FPAL_MAX_TEMPLATES);
	for (size_t i = 0; i < n; i++) {
		printf("  [%zu] ", i);
		print_id(&ids[i]);
	}
	return 0;
}

/* Parse "-t ms"; returns 0 on success, 2 on a usage error. */
static int parse_timeout(int argc, char **argv, uint32_t *timeout_ms) {
	int opt;

	*timeout_ms = FPAL_FINGER_WAIT_TIMEOUT_MS;
	while ((opt = getopt(argc, argv, "t:")) != -1) {
		char *end;
		unsigned long v;

		if (opt != 't') {
			usage(stderr);
			return 2;
		}
		v = strtoul(optarg, &end, 10);
		if (*end || v == 0 || v > UINT32_MAX) {
			fprintf(stderr, "fpal: invalid timeout '%s'\n", optarg);
			return 2;
		}
		*timeout_ms = (uint32_t)v;
	}
	return 0;
}

/* Open the sensor, wait for one finger and identify it. */
static int identify_once(uint32_t timeout_ms, struct fpal_template_id *match) {
	struct fpal_device *dev;
	int err;

	err = open_device(&dev);
	if (err)
		return err;

	printf("Touch the sensor (%u s, Ctrl-C to cancel)...\n",
	       timeout_ms / 1000u);
	fflush(stdout);

	err = fpal_identify(dev, timeout_ms, match);
	close_device(dev);
	return err;
}

static int cmd_identify(int argc, char **argv) {
	struct fpal_template_id match;
	uint32_t timeout_ms;
	int err;

	if (parse_timeout(argc, argv, &timeout_ms))
		return 2;

	err = identify_once(timeout_ms, &match);
	if (err == FPAL_E_NOMATCH) {
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
	if (!fpal_bindings_valid_user(user) || !getpwnam(user)) {
		fprintf(stderr, "fpal: no such user '%s'\n", user);
		return NULL;
	}
	return user;
}

static int cmd_bind(int argc, char **argv) {
	struct fpal_template_id match;
	uint32_t timeout_ms;
	const char *user;
	int err;

	if (parse_timeout(argc, argv, &timeout_ms))
		return 2;
	user = user_arg(argc, argv);
	if (!user)
		return 2;

	err = identify_once(timeout_ms, &match);
	if (err == FPAL_E_NOMATCH) {
		fputs("fpal: finger not enrolled on the sensor; enroll it in "
		      "Windows Hello first\n",
		      stderr);
		return 1;
	}
	if (err)
		return report(err);

	err = fpal_bindings_add(FPAL_STATE_DIR, user, &match);
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
	err = fpal_bindings_clear(FPAL_STATE_DIR, user);
	if (err)
		return report(err);
	printf("removed all bindings of %s (sensor untouched)\n", user);
	return 0;
}

static int cmd_bindings(int argc, char **argv) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	const char *user;
	size_t n = 0;
	int err;

	optind = 1;
	user = user_arg(argc, argv);
	if (!user)
		return 2;

	err = fpal_bindings_load(FPAL_STATE_DIR, user, ids, FPAL_MAX_TEMPLATES,
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

#ifdef FPAL_ENABLE_ENROLL
static void on_enroll_event(enum fpal_enroll_event ev, uint8_t stage,
			    uint8_t total, void *ud) {
	(void)ud;
	switch (ev) {
	case FPAL_ENROLL_TOUCH:
		if (stage == 0)
			puts("Touch the sensor once so it can check that this "
			     "finger is new...");
		else
			printf("Scan %u of %u: touch the sensor...\n", stage + 1u,
			       total);
		break;
	case FPAL_ENROLL_STAGE_DONE:
		printf("  accepted (%u/%u)\n", stage, total);
		break;
	case FPAL_ENROLL_RETRY_CENTER:
		puts("  finger off centre, touch again");
		break;
	case FPAL_ENROLL_RETRY_DIRTY:
		puts("  the sensor looks dirty: clean it and touch again");
		break;
	case FPAL_ENROLL_RETRY:
		puts("  could not read that, lift your finger and touch again");
		break;
	}
	fflush(stdout);
}

/* The last question before the sensor is written to. */
static bool confirm_store(size_t used) {
	char line[16];

	if (!isatty(STDIN_FILENO)) {
		fputs("fpal: storing a fingerprint needs a terminal to confirm\n",
		      stderr);
		return false;
	}
	fprintf(stderr,
		"This stores a NEW fingerprint on the sensor (%zu of %u slots in use).\n"
		"Fingerprints already stored, including those from Windows, are not touched.\n"
		"Type 'yes' to continue: ",
		used, FPAL_MAX_TEMPLATES);
	if (!fgets(line, sizeof(line), stdin) || strcmp(line, "yes\n") != 0) {
		fputs("fpal: cancelled, nothing was stored\n", stderr);
		return false;
	}
	return true;
}

static int cmd_enroll(int argc, char **argv) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES], id;
	bool dry_run = false, allow = false;
	const char *user = NULL;
	struct fpal_device *dev;
	size_t before = 0, after = 0;
	int err;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--dry-run"))
			dry_run = true;
		else if (!strcmp(argv[i], "--allow-chip-write"))
			allow = true;
		else if (argv[i][0] != '-' && !user)
			user = argv[i];
		else {
			usage(stderr);
			return 2;
		}
	}
	if (!user || !fpal_bindings_valid_user(user) || !getpwnam(user)) {
		fprintf(stderr, "fpal: no such user '%s'\n", user ? user : "");
		return 2;
	}
	if (!dry_run && !allow) {
		fputs("fpal: enrolling stores data on the sensor.\n"
		      "      add --allow-chip-write to do it, or --dry-run to try "
		      "every step without storing\n",
		      stderr);
		return 2;
	}

	err = open_device(&dev);
	if (err)
		return report(err);
	err = fpal_list(dev, ids, FPAL_MAX_TEMPLATES, &before);
	if (err) {
		close_device(dev);
		return report(err);
	}
	printf("%zu fingerprint(s) on the sensor before\n", before);

	if (!dry_run && !confirm_store(before)) {
		close_device(dev);
		return 1;
	}
	err = fpal_template_id_generate(&id);
	if (!err)
		err = fpal_enroll(dev, &id, dry_run ? FPAL_ENROLL_DRY_RUN : 0,
				  on_enroll_event, NULL);
	if (err == FPAL_E_EXISTS) {
		fputs("fpal: this finger (or id) is already enrolled; use "
		      "'fpal bind' instead\n",
		      stderr);
		close_device(dev);
		return 1;
	}
	if (err) {
		close_device(dev);
		return report(err);
	}

	err = fpal_list(dev, ids, FPAL_MAX_TEMPLATES, &after);
	close_device(dev);
	if (err)
		return report(err);

	if (dry_run) {
		printf("dry run finished; %zu fingerprint(s) on the sensor after\n",
		       after);
		if (after != before) {
			fputs("fpal: the sensor's list CHANGED during a dry run\n",
			      stderr);
			return 1;
		}
		puts("nothing was stored");
		return 0;
	}

	err = fpal_bindings_add(FPAL_STATE_DIR, user, &id);
	if (err) {
		fputs("fpal: the finger is stored but could not be bound; "
		      "bind it by hand. New id: ",
		      stderr);
		print_id(&id);
		return report(err);
	}
	printf("enrolled and bound to %s: ", user);
	print_id(&id);
	return 0;
}
#else
static int cmd_enroll(int argc, char **argv) {
	(void)argc, (void)argv;
	fputs("fpal: this build cannot enroll; rebuild with: make ENROLL=1\n",
	      stderr);
	return 1;
}
#endif

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
	if (strcmp(cmd, "enroll") == 0)
		return cmd_enroll(argc - 1, argv + 1);
	if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
		puts("fpal " FPAL_VERSION);
		return 0;
	}
	if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 ||
	    strcmp(cmd, "--help") == 0) {
		usage(stdout);
		return 0;
	}

	fprintf(stderr, "fpal: unknown command '%s'\n", cmd);
	usage(stderr);
	return 2;
}
