// SPDX-License-Identifier: Apache-2.0
/*
 * The fpal manage screen: keys, the line editor, drawing, and whole sessions
 * driven through a pipe against a fake sensor. The point of the session tests
 * is that nothing is deleted or stored without the full typed confirmation.
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../tools/manage.h"
#include "fpal/bindings.h"
#include "fpal/labels.h"
#include "t.h"

/* ---- keys ---------------------------------------------------------------- */

static struct fpal_key key1(const char *bytes, bool final, size_t *used) {
	struct fpal_key k;

	*used = fpal_key_parse((const unsigned char *)bytes, strlen(bytes), final, &k);
	return k;
}

static void test_keys_plain_and_arrows(void) {
	size_t u;

	T_EQ_INT(key1("\x1b[A", false, &u).type, K_UP);
	T_EQ_INT(u, 3);
	T_EQ_INT(key1("\x1b[B", false, &u).type, K_DOWN);
	T_EQ_INT(key1("\x1bOA", false, &u).type, K_UP); /* application mode */
	T_EQ_INT(key1("\x1b[H", false, &u).type, K_HOME);
	T_EQ_INT(key1("\x1b[F", false, &u).type, K_END);
	T_EQ_INT(key1("\x1b[1~", false, &u).type, K_HOME);
	T_EQ_INT(key1("\x1b[4~", false, &u).type, K_END);
	T_EQ_INT(key1("\r", false, &u).type, K_ENTER);
	T_EQ_INT(key1("\r\n", false, &u).type, K_ENTER);
	T_EQ_INT(u, 2);
	T_EQ_INT(key1("\x7f", false, &u).type, K_BACKSPACE);
	T_EQ_INT(key1("\x08", false, &u).type, K_BACKSPACE);
	T_EQ_INT(key1("d", false, &u).type, K_CHAR);
	T_EQ_INT(key1("d", false, &u).ch, 'd');
}

static void test_keys_incomplete_input(void) {
	size_t u;

	T_EQ_INT(key1("\x1b", false, &u).type, K_NONE);
	T_EQ_INT(u, 0); /* might be the start of an arrow key */
	T_EQ_INT(key1("\x1b", true, &u).type, K_ESC);
	T_EQ_INT(u, 1);
	T_EQ_INT(key1("\x1b[", false, &u).type, K_NONE);
	T_EQ_INT(u, 0);
	key1("\x1b[", true, &u);
	T_EQ_INT(u, 2); /* dropped, not stuck */
	key1("\xc4", false, &u);
	T_EQ_INT(u, 0); /* half of a UTF-8 character */
	T_EQ_INT(key1("\x1bq", false, &u).type, K_ESC);
	T_EQ_INT(u, 1); /* Escape, then q is left for the next key */
}

static void test_keys_unknown_are_consumed(void) {
	size_t u;

	T_EQ_INT(key1("\x1b[3~", false, &u).type, K_NONE); /* Delete */
	T_EQ_INT(u, 4);
	T_EQ_INT(key1("\x1b[1;5A", false, &u).type, K_UP); /* Ctrl-Up */
	T_EQ_INT(u, 6);
	T_EQ_INT(key1("\x01", false, &u).type, K_NONE);
	T_EQ_INT(u, 1);
	T_EQ_INT(key1("\x80", false, &u).type, K_NONE); /* stray continuation */
	T_EQ_INT(u, 1);
}

static void test_keys_utf8(void) {
	struct fpal_key k;
	size_t u;

	k = key1("\xc4\x9f", false, &u); /* ğ */
	T_EQ_INT(k.type, K_CHAR);
	T_EQ_INT(k.ch, 0x11f);
	T_EQ_INT(u, 2);
	k = key1("\xe2\x82\xac", false, &u); /* euro sign */
	T_EQ_INT(k.ch, 0x20ac);
	T_EQ_INT(u, 3);
	key1("\xc4" "x", false, &u); /* broken: continuation missing */
	T_EQ_INT(u, 1);
}

/* ---- the line editor ----------------------------------------------------- */

static void type(struct fpal_line *l, const char *bytes) {
	size_t i = 0, len = strlen(bytes);

	while (i < len) {
		struct fpal_key k;
		size_t u = fpal_key_parse((const unsigned char *)bytes + i, len - i,
					  true, &k);

		if (!u)
			break;
		fpal_line_feed(l, &k);
		i += u;
	}
}

static void test_line_editor(void) {
	struct fpal_line l;
	struct fpal_key enter = { K_ENTER, 0 }, esc = { K_ESC, 0 };

	fpal_line_set(&l, "");
	type(&l, "Sa\xc4\x9f");
	T_CHECK(!strcmp(l.text, "Sa\xc4\x9f"));
	type(&l, "\x7f"); /* one Backspace removes the whole 2 byte character */
	T_CHECK(!strcmp(l.text, "Sa"));
	type(&l, "\x7f\x7f\x7f"); /* more than there is */
	T_EQ_INT(l.len, 0);
	T_EQ_INT(fpal_line_feed(&l, &enter), LINE_DONE);
	T_EQ_INT(fpal_line_feed(&l, &esc), LINE_CANCELLED);

	fpal_line_set(&l, "old");
	type(&l, "\x01\t"); /* control characters are ignored */
	T_CHECK(!strcmp(l.text, "old"));
}

static void test_line_editor_is_bounded(void) {
	struct fpal_line l;
	char big[400];

	memset(big, 'x', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	fpal_line_set(&l, "");
	type(&l, big);
	T_CHECK(l.len < FPAL_LINE_MAX);
	T_EQ_INT(l.text[l.len], '\0');
}

/* ---- a fake sensor and a session driver ----------------------------------- */

struct fake {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	size_t n;
	unsigned removed, enrolled, listed;
	struct fpal_template_id last_removed;
	int list_rc, remove_rc, enroll_rc;
	unsigned next_new;
};

static struct fake sensor;
static char dir[64];
static struct fpal_ui plain, fancy;

static int f_list(void *ud, struct fpal_template_id *ids, size_t cap, size_t *n) {
	(void)ud, (void)cap;
	sensor.listed++;
	if (sensor.list_rc)
		return sensor.list_rc;
	memcpy(ids, sensor.ids, sensor.n * sizeof(ids[0]));
	*n = sensor.n;
	return FPAL_OK;
}

static int f_remove(void *ud, const struct fpal_template_id *id) {
	(void)ud;
	if (sensor.remove_rc)
		return sensor.remove_rc;
	for (size_t i = 0; i < sensor.n; i++) {
		if (!memcmp(sensor.ids[i].bytes, id->bytes, FPAL_TEMPLATE_ID_LEN)) {
			memmove(&sensor.ids[i], &sensor.ids[i + 1],
				(sensor.n - i - 1) * sizeof(sensor.ids[0]));
			sensor.n--;
			sensor.removed++;
			sensor.last_removed = *id;
			return FPAL_OK;
		}
	}
	return FPAL_E_NODATA;
}

static int f_new_id(struct fpal_template_id *id) {
	memset(id->bytes, 0, FPAL_TEMPLATE_ID_LEN);
	snprintf((char *)id->bytes, FPAL_TEMPLATE_ID_LEN, "FPAL-new-%u", ++sensor.next_new);
	return FPAL_OK;
}

static int f_enroll(void *ud, const struct fpal_template_id *id, fpal_enroll_cb cb,
		    void *cb_ud) {
	(void)ud;
	if (cb) {
		cb(FPAL_ENROLL_CHECK_TOUCH, 0, 20, cb_ud);
		cb(FPAL_ENROLL_STAGE_DONE, 1, 20, cb_ud);
	}
	if (sensor.enroll_rc)
		return sensor.enroll_rc;
	sensor.ids[sensor.n++] = *id;
	sensor.enrolled++;
	return FPAL_OK;
}

static struct fpal_manage_ops ops = {
	.can_write = true, .list = f_list, .remove = f_remove,
	.new_id = f_new_id, .enroll = f_enroll,
};

static void reset(void) {
	char cmd[256];

	memset(&sensor, 0, sizeof(sensor));
	/* [0] binary like a Windows id, [1] made by fpal */
	for (size_t i = 0; i < FPAL_TEMPLATE_ID_LEN; i++)
		sensor.ids[0].bytes[i] = (uint8_t)(0x7d + i * 7);
	memcpy(sensor.ids[1].bytes, "FPAL-7b516a3ef8dab92ad49f9124678", 32);
	sensor.n = 2;
	ops.can_write = true;

	snprintf(cmd, sizeof(cmd), "rm -rf %s/* %s/.labels", dir, dir);
	if (system(cmd) != 0)
		fputs("cleanup failed\n", stderr);
	fpal_labels_set(dir, &sensor.ids[0], "Windows finger");
	fpal_labels_set(dir, &sensor.ids[1], "Linux finger");
	fpal_bindings_add(dir, "alice", &sensor.ids[0]);
	fpal_bindings_add(dir, "alice", &sensor.ids[1]);
	fpal_bindings_add(dir, "bob", &sensor.ids[1]);
}

/* Feed @keys to a session and return what it drew (caller frees). */
static char *session(const char *keys) {
	int fds[2];
	char *buf = NULL;
	size_t len = 0;
	FILE *out = open_memstream(&buf, &len);

	T_EQ_INT(pipe(fds), 0);
	T_EQ_INT(write(fds[1], keys, strlen(keys)), (ssize_t)strlen(keys));
	close(fds[1]);
	fpal_manage_run(&ops, &plain, fds[0], out, dir, "alice");
	close(fds[0]);
	fclose(out);
	return buf;
}

static bool has(const char *haystack, const char *needle) {
	return haystack && strstr(haystack, needle) != NULL;
}

static bool labelled(size_t i, const char *want) {
	char l[FPAL_LABEL_MAX + 1];

	return !fpal_labels_get(dir, &sensor.ids[i], l, sizeof(l)) && !strcmp(l, want);
}

/* ---- the screen ---------------------------------------------------------- */

static void test_draw_shows_fingers_and_selection(void) {
	char *buf = NULL;
	size_t len = 0;
	FILE *f = open_memstream(&buf, &len);
	struct fpal_manage_view v = {
		.ids = sensor.ids, .n = 2, .sel = 1, .state_dir = dir,
		.user = "alice", .can_write = true,
	};

	reset();
	fpal_manage_draw(&plain, f, &v);
	fclose(f);
	T_CHECK(has(buf, "Windows finger"));
	T_CHECK(has(buf, "Linux finger"));
	T_CHECK(has(buf, "FPAL-7b516a3ef8d"));
	T_CHECK(has(buf, "> 1  Linux finger")); /* the pointer is on the selected row */
	T_CHECK(!has(buf, "> 0"));
	T_CHECK(has(buf, "d delete"));
	T_CHECK(has(buf, "2 of 10 fingerprints"));
	free(buf);
}

static void test_draw_read_only_has_no_delete_key(void) {
	char *buf = NULL;
	size_t len = 0;
	FILE *f = open_memstream(&buf, &len);
	struct fpal_manage_view v = { .ids = sensor.ids, .n = 2, .state_dir = dir,
				      .user = "alice", .can_write = false };

	reset();
	fpal_manage_draw(&plain, f, &v);
	fclose(f);
	T_CHECK(has(buf, "r rename"));
	T_CHECK(!has(buf, "d delete"));
	T_CHECK(!has(buf, "new finger"));
	free(buf);
}

static void test_draw_utf8_selection_is_reversed(void) {
	char *buf = NULL;
	size_t len = 0;
	FILE *f = open_memstream(&buf, &len);
	struct fpal_manage_view v = { .ids = sensor.ids, .n = 2, .sel = 0,
				      .state_dir = dir, .user = "alice" };

	reset();
	fpal_manage_draw(&fancy, f, &v);
	fclose(f);
	T_CHECK(has(buf, "\033[7m")); /* reverse video on the selected row */
	T_CHECK(has(buf, "\xe2\x96\xb8")); /* pointer */
	free(buf);
}

/* ---- sessions ------------------------------------------------------------ */

static void test_navigation_and_quit(void) {
	char *out;

	reset();
	out = session("\x1b[B" "\x1b[A" "j" "k" "q");
	T_CHECK(has(out, "Linux finger"));
	T_EQ_INT(sensor.removed, 0);
	free(out);

	reset();
	out = session(""); /* end of input ends the session */
	T_CHECK(out != NULL);
	free(out);
}

static void test_delete_needs_the_word_yes(void) {
	char *out;

	reset();
	out = session("j" "d" "yes\r" "q"); /* select Linux finger, delete */
	T_EQ_INT(sensor.removed, 1);
	T_EQ_MEM(sensor.last_removed.bytes, "FPAL-7b516a3ef8dab92ad49f9124678", 32);
	T_EQ_INT(sensor.n, 1);
	T_CHECK(has(out, "deleted; 1 fingerprint(s) left"));
	/* what pointed at it is gone: its name and both users' bindings */
	T_CHECK(!fpal_bindings_contains(dir, "alice", &sensor.last_removed));
	T_CHECK(!fpal_bindings_contains(dir, "bob", &sensor.last_removed));
	T_CHECK(fpal_bindings_contains(dir, "alice", &sensor.ids[0]));
	free(out);
}

static void test_wrong_confirmation_deletes_nothing(void) {
	static const char *const tries[] = {
		"j" "d" "no\r" "q",	  "j" "d" "\r" "q",
		"j" "d" "ye\r" "q",	  "j" "d" "YES\r" "q",
		"j" "d" "yes \r" "q",	  "j" "d" "yess\r" "q",
		"j" "d" "DELETE\r" "q",   /* the foreign word is not for our own finger */
	};
	char *out;

	for (size_t i = 0; i < sizeof(tries) / sizeof(tries[0]); i++) {
		reset();
		out = session(tries[i]);
		T_EQ_INT(sensor.removed, 0);
		T_EQ_INT(sensor.n, 2);
		T_CHECK(has(out, "not confirmed"));
		free(out);
	}
}

static void test_escape_cancels_a_delete(void) {
	char *out;

	reset();
	out = session("j" "d" "yes" "\x1b" "q"); /* typed yes but pressed Escape */
	T_EQ_INT(sensor.removed, 0);
	T_CHECK(has(out, "cancelled"));
	free(out);
}

static void test_keys_in_the_list_never_delete(void) {
	char *out;

	reset();
	out = session("yes" "\r" "DELETE" "xyz" "q"); /* nothing here opens a prompt */
	T_EQ_INT(sensor.removed, 0);
	free(out);
}

static void test_foreign_finger_needs_DELETE(void) {
	char *out;

	reset();
	out = session("d" "yes\r" "q"); /* finger 0 was not made by fpal */
	T_EQ_INT(sensor.removed, 0);
	free(out);

	reset();
	out = session("d" "delete\r" "q"); /* case matters */
	T_EQ_INT(sensor.removed, 0);
	free(out);

	reset();
	out = session("d" "DELETE\r" "q");
	T_EQ_INT(sensor.removed, 1);
	T_CHECK(has(out, "NOT created by fpal"));
	free(out);
}

static void test_failed_delete_is_reported(void) {
	char *out;

	reset();
	sensor.remove_rc = FPAL_E_PROTO;
	out = session("j" "d" "yes\r" "q");
	T_CHECK(has(out, "not deleted"));
	T_EQ_INT(sensor.n, 2);
	T_CHECK(labelled(1, "Linux finger")); /* its name is kept: it is still there */
	free(out);
}

static void test_rename(void) {
	char *out;

	reset();
	/* the editor starts with the current name: erase 14 characters first */
	out = session("r" "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f"
		      "Sa\xc4\x9f i\xc5\x9f" "aret\r" "q");
	T_CHECK(labelled(0, "Sa\xc4\x9f i\xc5\x9f" "aret"));
	T_CHECK(has(out, "is now called"));
	T_CHECK(labelled(1, "Linux finger")); /* the other one is untouched */
	free(out);
}

static void test_rename_empty_removes_and_bad_names_are_refused(void) {
	char *out;

	reset();
	out = session("j" "r" "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f" "\r" "q");
	T_CHECK(labelled(1, ""));
	T_CHECK(has(out, "removed"));
	free(out);

	reset();
	out = session("r" "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f"
		      " x\r" "q"); /* a leading space is not a usable name */
	T_CHECK(has(out, "not a usable name"));
	T_CHECK(labelled(0, "Windows finger")); /* unchanged */
	free(out);

	reset();
	out = session("r" "extra" "\x1b" "q"); /* Escape: no change */
	T_CHECK(labelled(0, "Windows finger"));
	free(out);
}

static void test_new_finger(void) {
	struct fpal_template_id fresh;
	char *out;

	reset();
	out = session("n" "yes\r" "Right thumb\r" "q");
	T_EQ_INT(sensor.enrolled, 1);
	T_EQ_INT(sensor.n, 3);
	fresh = sensor.ids[2];
	T_CHECK(fpal_bindings_contains(dir, "alice", &fresh)); /* bound to the user */
	T_CHECK(!fpal_bindings_contains(dir, "bob", &fresh));
	T_CHECK(labelled(2, "Right thumb"));
	T_CHECK(has(out, "new finger stored and bound to alice"));
	T_CHECK(has(out, "accepted (1/20)")); /* progress was shown */
	free(out);
}

static void test_new_finger_needs_yes_and_can_skip_the_name(void) {
	char *out;

	reset();
	out = session("n" "no\r" "q");
	T_EQ_INT(sensor.enrolled, 0);
	T_CHECK(has(out, "nothing was stored"));
	free(out);

	reset();
	out = session("n" "yes\r" "\r" "q"); /* Enter at the name prompt skips it */
	T_EQ_INT(sensor.enrolled, 1);
	T_CHECK(labelled(2, ""));
	free(out);
}

static void test_new_finger_failures(void) {
	char *out;

	reset();
	sensor.enroll_rc = FPAL_E_EXISTS;
	out = session("n" "yes\r" "q");
	T_CHECK(has(out, "already enrolled"));
	T_EQ_INT(sensor.n, 2);
	free(out);

	reset();
	sensor.n = FPAL_MAX_TEMPLATES; /* a full sensor never even asks */
	out = session("n" "q");
	T_CHECK(has(out, "sensor is full"));
	T_EQ_INT(sensor.enrolled, 0);
	free(out);
}

static void test_read_only_build_cannot_write(void) {
	char *out;

	reset();
	ops.can_write = false;
	out = session("j" "d" "yes\r" "n" "yes\r" "q");
	T_EQ_INT(sensor.removed, 0);
	T_EQ_INT(sensor.enrolled, 0);
	T_CHECK(has(out, "read-only"));
	free(out);
}

static void test_unreadable_sensor_ends_the_session(void) {
	int fds[2];
	char *buf = NULL;
	size_t len = 0;
	FILE *out = open_memstream(&buf, &len);

	reset();
	sensor.list_rc = FPAL_E_PERM;
	T_EQ_INT(pipe(fds), 0);
	close(fds[1]);
	T_EQ_INT(fpal_manage_run(&ops, &plain, fds[0], out, dir, "alice"), FPAL_E_PERM);
	close(fds[0]);
	fclose(out);
	T_EQ_INT(len, 0); /* nothing is drawn: the caller reports it */
	free(buf);
}

int main(void) {
	char tmpl[] = "/tmp/fpal-manage-XXXXXX";

	if (!mkdtemp(tmpl)) {
		perror("mkdtemp");
		return 1;
	}
	snprintf(dir, sizeof(dir), "%s", tmpl);
	plain = fpal_ui_make(false, NULL, NULL, NULL);
	fancy = fpal_ui_make(true, NULL, "xterm-256color", "en_US.UTF-8");

	T_RUN(test_keys_plain_and_arrows);
	T_RUN(test_keys_incomplete_input);
	T_RUN(test_keys_unknown_are_consumed);
	T_RUN(test_keys_utf8);
	T_RUN(test_line_editor);
	T_RUN(test_line_editor_is_bounded);
	T_RUN(test_draw_shows_fingers_and_selection);
	T_RUN(test_draw_read_only_has_no_delete_key);
	T_RUN(test_draw_utf8_selection_is_reversed);
	T_RUN(test_navigation_and_quit);
	T_RUN(test_delete_needs_the_word_yes);
	T_RUN(test_wrong_confirmation_deletes_nothing);
	T_RUN(test_escape_cancels_a_delete);
	T_RUN(test_keys_in_the_list_never_delete);
	T_RUN(test_foreign_finger_needs_DELETE);
	T_RUN(test_failed_delete_is_reported);
	T_RUN(test_rename);
	T_RUN(test_rename_empty_removes_and_bad_names_are_refused);
	T_RUN(test_new_finger);
	T_RUN(test_new_finger_needs_yes_and_can_skip_the_name);
	T_RUN(test_new_finger_failures);
	T_RUN(test_read_only_build_cannot_write);
	T_RUN(test_unreadable_sensor_ends_the_session);

	{
		char cmd[96];

		snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpl);
		if (system(cmd) != 0)
			fputs("cleanup failed\n", stderr);
	}
	return T_DONE();
}
