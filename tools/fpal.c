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
 *   fpal label <finger> [name]   show, set or --clear the name of a finger
 *   fpal status [user]           is fingerprint login set up, and if not what to do
 *   fpal setup [--revert]        switch sudo, su and GNOME to the fingerprint, or back
 *   fpal enroll [--dry-run] [--allow-chip-write] [--name N] <user>
 *                                store a NEW finger on the sensor
 *   fpal delete [--dry-run] [--allow-chip-write] [--allow-foreign] <finger>
 *                                delete ONE finger from the sensor
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
#include "fpal/labels.h"
#include "finger.h"
#include "manage.h"
#include "setup.h"
#include "status.h"
#include "ui.h"

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
	      "  list [--raw]      list the fingers stored on the sensor\n"
	      "  identify [-t ms]  wait for a finger and show the matching id\n"
	      "  bind [-t ms] USER bind the touched finger to USER\n"
	      "  unbind USER       forget USER's bindings (sensor untouched)\n"
	      "  bindings USER     show the template ids bound to USER\n"
	      "  label FINGER [NAME|--clear]\n"
	      "                    show, set or clear the name of a finger\n"
	      "  manage            pick fingers with the arrow keys: rename, delete, add\n"
	      "  status [USER]     show whether fingerprint login is set up\n"
	      "  setup [--revert]  make sudo, su and the GNOME screens use the fingerprint\n"
#ifdef FPAL_ENABLE_ENROLL
	      "  enroll [--dry-run] [--allow-chip-write] [--name NAME] USER\n"
	      "                    store a new finger on the sensor and bind it to USER\n"
	      "  delete [--dry-run] [--allow-chip-write] [--allow-foreign] FINGER\n"
	      "                    delete ONE finger from the sensor (cannot be undone)\n"
#endif
	      "  version           show the fpal version\n"
	      "  help              show this help\n"
	      "\n"
	      "FINGER is its number in 'fpal list', its name, or the start of its id.\n"
	      "\n"
	      "environment:\n"
	      "  " FPAL_ENV_TRACE "=1   hex dump every USB packet\n"
	      "  " FPAL_ENV_DEBUG "=1   verbose log messages\n",
	      f);
}

static void print_id(const struct fpal_template_id *id) {
	char label[FPAL_LABEL_MAX + 1];
	char hex[FPAL_ID_HEX_MAX + 1], text[FPAL_ID_HEX_MAX / 2 + 1];

	(void)fpal_id_to_hex(id, hex, sizeof(hex));
	fpal_id_text(id, text, sizeof(text));
	printf("%s  \"%s\"", hex, text);
	if (!fpal_labels_get(FPAL_STATE_DIR, id, label, sizeof(label)) &&
	    label[0])
		printf("  %s", label);
	putchar('\n');
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

/* The list as a table, for a person at a terminal. */
static void print_table(const struct fpal_ui *ui,
			const struct fpal_template_id *ids, size_t n, size_t max) {
	static const char *const head[] = { "#", "Name", "Kind", "Yours", "Id" };
	char num[FPAL_MAX_TEMPLATES][8], name[FPAL_MAX_TEMPLATES][FPAL_LABEL_MAX + 12];
	char sid[FPAL_MAX_TEMPLATES][40];
	const char *cells[FPAL_MAX_TEMPLATES * 5];
	const char *user = fpal_ui_invoking_user();

	for (size_t i = 0; i < n; i++) {
		char label[FPAL_LABEL_MAX + 1] = "";

		(void)fpal_labels_get(FPAL_STATE_DIR, &ids[i], label, sizeof(label));
		snprintf(num[i], sizeof(num[i]), "%zu", i);
		snprintf(name[i], sizeof(name[i]), "%s", label[0] ? label : "(no name)");
		fpal_ui_short_id(ui, &ids[i], sid[i], sizeof(sid[i]));
		cells[i * 5 + 0] = num[i];
		cells[i * 5 + 1] = name[i];
		cells[i * 5 + 2] = fpal_finger_is_ours(&ids[i]) ? "FPAL" : "other";
		cells[i * 5 + 3] = fpal_bindings_contains(FPAL_STATE_DIR, user, &ids[i]) ?
					   fpal_ui_mark(ui, UI_OK) :
					   "";
		cells[i * 5 + 4] = sid[i];
	}
	printf("\n  %s%zu fingerprint(s)%s on the sensor (it holds up to %zu)\n\n",
	       fpal_ui_style(ui, UI_BOLD), n, fpal_ui_style(ui, UI_RESET), max);
	if (n)
		fpal_ui_table(ui, stdout, 5, head, n, cells);
	printf("\n  %s\"Yours\" = bound to %s. Kind \"other\" = not created by fpal, "
	       "for example Windows Hello.%s\n\n",
	       fpal_ui_style(ui, UI_DIM), user, fpal_ui_style(ui, UI_RESET));
}

static int cmd_list(int argc, char **argv) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	struct fpal_ui ui = fpal_ui_init(stdout);
	struct fpal_device *dev;
	size_t n = 0, max;
	int err;

	if (argc > 2 || (argc == 2 && strcmp(argv[1], "--raw") != 0)) {
		usage(stderr);
		return 2;
	}
	/* --raw, or output that is not a terminal, keeps the format scripts rely on */
	if (argc == 2)
		ui.fancy = false;

	err = open_device(&dev);
	if (err)
		return report(err);

	err = fpal_list(dev, ids, FPAL_MAX_TEMPLATES, &n);
	max = fpal_device_max_templates(dev);
	close_device(dev);
	if (err)
		return report(err);

	if (ui.fancy) {
		print_table(&ui, ids, n, max);
		return 0;
	}
	printf("%zu template(s) stored on the sensor (max %zu)\n", n, max);
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

/* ---- naming and picking a finger --------------------------------------- */

/* Print why a finger could not be picked. */
static int resolve_finger(const struct fpal_template_id *ids, size_t n,
			  const char *arg, size_t *index) {
	size_t matches = 0;
	int err = fpal_finger_find(FPAL_STATE_DIR, ids, n, arg, index, &matches);

	if (err == FPAL_E_EXISTS)
		fprintf(stderr, "fpal: '%s' matches %zu fingers, be more specific\n",
			arg, matches);
	else if (err == FPAL_E_NODATA)
		fprintf(stderr, "fpal: no finger matches '%s' (the sensor holds %zu; see: fpal list)\n",
			arg, n);
	return err;
}

static int cmd_label(int argc, char **argv) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	struct fpal_device *dev;
	const char *name = argc > 2 ? argv[2] : NULL;
	bool clear = name && !strcmp(name, "--clear");
	char label[FPAL_LABEL_MAX + 1];
	size_t n = 0, i = 0;
	int err;

	if (argc < 2 || argc > 3) {
		usage(stderr);
		return 2;
	}
	if (name && !clear && !fpal_label_valid(name)) {
		fprintf(stderr, "fpal: a name is 1 to %u bytes with no control "
				"characters and no leading or trailing space\n",
			FPAL_LABEL_MAX);
		return 2;
	}

	err = open_device(&dev);
	if (err)
		return report(err);
	err = fpal_list(dev, ids, FPAL_MAX_TEMPLATES, &n);
	close_device(dev);
	if (err)
		return report(err);
	if (resolve_finger(ids, n, argv[1], &i))
		return 1;

	if (!name) {
		err = fpal_labels_get(FPAL_STATE_DIR, &ids[i], label, sizeof(label));
		if (err)
			return report(err);
		printf("[%zu] %s\n", i, label[0] ? label : "(no name)");
		return 0;
	}
	err = fpal_labels_set(FPAL_STATE_DIR, &ids[i], clear ? NULL : name);
	if (err)
		return report(err);
	if (clear)
		printf("[%zu] name removed\n", i);
	else
		printf("[%zu] is now called \"%s\"\n", i, name);
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
	case FPAL_ENROLL_CHECK_TOUCH:
		puts("Touch the sensor once so it can check that this finger "
		     "is new...");
		break;
	case FPAL_ENROLL_TOUCH:
		printf("Scan %u of %u: touch the sensor...\n", stage + 1u, total);
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

/* The last question before the sensor is written to: type @phrase exactly. */
static bool confirm(const char *message, const char *phrase) {
	char line[32], want[32];

	if (!isatty(STDIN_FILENO)) {
		fputs("fpal: this needs a terminal to confirm\n", stderr);
		return false;
	}
	snprintf(want, sizeof(want), "%s\n", phrase);
	fprintf(stderr, "%s\nType '%s' to continue: ", message, phrase);
	if (!fgets(line, sizeof(line), stdin) || strcmp(line, want) != 0) {
		fputs("fpal: cancelled, the sensor was not changed\n", stderr);
		return false;
	}
	return true;
}

static bool confirm_store(size_t used, size_t max) {
	char msg[256];

	snprintf(msg, sizeof(msg),
		 "This stores a NEW fingerprint on the sensor (%zu of %zu slots in use).\n"
		 "Fingerprints already stored, including those from Windows, are not touched.",
		 used, max);
	return confirm(msg, "yes");
}

static int cmd_enroll(int argc, char **argv) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES], id;
	bool dry_run = false, allow = false;
	const char *user = NULL, *name = NULL;
	struct fpal_device *dev;
	size_t before = 0, after = 0;
	int err;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--dry-run"))
			dry_run = true;
		else if (!strcmp(argv[i], "--allow-chip-write"))
			allow = true;
		else if (!strcmp(argv[i], "--name") && i + 1 < argc)
			name = argv[++i];
		else if (argv[i][0] != '-' && !user)
			user = argv[i];
		else {
			usage(stderr);
			return 2;
		}
	}
	if (!user) {
		usage(stderr);
		return 2;
	}
	if (!fpal_bindings_valid_user(user) || !getpwnam(user)) {
		fprintf(stderr, "fpal: no such user '%s'\n", user);
		return 2;
	}
	/* A bad name must not be found out after the 20 scans. */
	if (name && !fpal_label_valid(name)) {
		fprintf(stderr, "fpal: a name is 1 to %u bytes with no control "
				"characters and no leading or trailing space\n",
			FPAL_LABEL_MAX);
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

	if (!dry_run && !confirm_store(before, fpal_device_max_templates(dev))) {
		close_device(dev);
		return 1;
	}
	err = fpal_template_id_new(dev, &id);
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
	if (name && fpal_labels_set(FPAL_STATE_DIR, &id, name))
		fputs("fpal: the finger is stored but its name could not be "
		      "saved; use 'fpal label'\n",
		      stderr);
	printf("enrolled and bound to %s: ", user);
	print_id(&id);
	return 0;
}

/*
 * Delete ONE template. Three things stand between a typo and a deleted
 * fingerprint: the build, --allow-chip-write and a typed confirmation, and
 * fingerprints FPAL did not create need --allow-foreign and a stricter word.
 */
static int cmd_delete(int argc, char **argv) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	bool dry_run = false, allow = false, foreign_ok = false;
	const char *target = NULL;
	struct fpal_template_id victim;
	struct fpal_device *dev;
	char msg[512];
	size_t n = 0, i = 0;
	int err;

	for (int k = 1; k < argc; k++) {
		if (!strcmp(argv[k], "--dry-run"))
			dry_run = true;
		else if (!strcmp(argv[k], "--allow-chip-write"))
			allow = true;
		else if (!strcmp(argv[k], "--allow-foreign"))
			foreign_ok = true;
		else if (argv[k][0] != '-' && !target)
			target = argv[k];
		else {
			usage(stderr);
			return 2;
		}
	}
	if (!target) {
		usage(stderr);
		return 2;
	}
	if (!dry_run && !allow) {
		fputs("fpal: deleting removes a fingerprint from the sensor "
		      "for good.\n"
		      "      add --allow-chip-write to do it, or --dry-run to "
		      "see what would be deleted\n",
		      stderr);
		return 2;
	}

	err = open_device(&dev);
	if (err)
		return report(err);
	err = fpal_list(dev, ids, FPAL_MAX_TEMPLATES, &n);
	if (err) {
		close_device(dev);
		return report(err);
	}
	if (resolve_finger(ids, n, target, &i)) {
		close_device(dev);
		return 1;
	}

	printf("finger to delete: [%zu] ", i);
	print_id(&ids[i]);
	if (!fpal_finger_is_ours(&ids[i]) && !foreign_ok) {
		fputs("fpal: this fingerprint was not created by FPAL (it may "
		      "come from Windows Hello).\n"
		      "      Deleting it removes it for every system that uses "
		      "this sensor. If you really\n"
		      "      mean it, add --allow-foreign\n",
		      stderr);
		close_device(dev);
		return 2;
	}
	if (dry_run) {
		printf("dry run: nothing was deleted (%zu fingerprint(s) on the sensor)\n",
		       n);
		close_device(dev);
		return 0;
	}

	if (fpal_finger_is_ours(&ids[i])) {
		snprintf(msg, sizeof(msg),
			 "This DELETES finger [%zu] from the sensor. It cannot be undone.\n"
			 "The other %zu fingerprint(s) are not touched.",
			 i, n - 1);
		if (!confirm(msg, "yes")) {
			close_device(dev);
			return 1;
		}
	} else {
		snprintf(msg, sizeof(msg),
			 "This DELETES finger [%zu], which FPAL did NOT create (Windows Hello?),\n"
			 "from the sensor. It stops working everywhere and cannot be undone.",
			 i);
		if (!confirm(msg, "DELETE")) {
			close_device(dev);
			return 1;
		}
	}

	victim = ids[i];
	err = fpal_delete(dev, &victim);
	if (err) {
		close_device(dev);
		fputs("fpal: the sensor's list did not end up as expected; run "
		      "'fpal list' to see what it holds\n",
		      stderr);
		return report(err);
	}
	err = fpal_list(dev, ids, FPAL_MAX_TEMPLATES, &n);
	close_device(dev);
	if (err)
		return report(err);

	/* The id is gone from the sensor: drop what pointed at it. */
	if (fpal_bindings_forget(FPAL_STATE_DIR, &victim) ||
	    fpal_labels_set(FPAL_STATE_DIR, &victim, NULL))
		fputs("fpal: deleted, but some bindings or its name could not "
		      "be cleaned up\n",
		      stderr);
	printf("deleted; %zu fingerprint(s) left on the sensor\n", n);
	return 0;
}
#else
static int cmd_enroll(int argc, char **argv) {
	(void)argc, (void)argv;
	fputs("fpal: this is a read-only build (made with ENROLL=0) and cannot enroll\n",
	      stderr);
	return 1;
}

static int cmd_delete(int argc, char **argv) {
	(void)argc, (void)argv;
	fputs("fpal: this is a read-only build (made with ENROLL=0) and cannot delete\n",
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
		return cmd_list(argc - 1, argv + 1);
	if (strcmp(cmd, "identify") == 0)
		return cmd_identify(argc - 1, argv + 1);
	if (strcmp(cmd, "bind") == 0)
		return cmd_bind(argc - 1, argv + 1);
	if (strcmp(cmd, "unbind") == 0)
		return cmd_unbind(argc - 1, argv + 1);
	if (strcmp(cmd, "bindings") == 0)
		return cmd_bindings(argc - 1, argv + 1);
	if (strcmp(cmd, "manage") == 0)
		return fpal_cmd_manage(argc - 1, argv + 1);
	if (strcmp(cmd, "status") == 0)
		return fpal_cmd_status(argc - 1, argv + 1);
	if (strcmp(cmd, "setup") == 0)
		return fpal_cmd_setup(argc - 1, argv + 1);
	if (strcmp(cmd, "label") == 0)
		return cmd_label(argc - 1, argv + 1);
	if (strcmp(cmd, "delete") == 0)
		return cmd_delete(argc - 1, argv + 1);
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
