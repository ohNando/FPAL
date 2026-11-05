// SPDX-License-Identifier: Apache-2.0
/*
 * Label file tests, run in a private temporary directory.
 */
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fpal/bindings.h"
#include "fpal/labels.h"
#include "id_util.h"
#include "t.h"

static char dir[64];

static void make_id(struct fpal_template_id *id, uint8_t seed) {
	id_seq(id, seed, TEST_ID_LEN);
}

static void test_valid_labels(void) {
	T_CHECK(fpal_label_valid("Windows finger"));
	T_CHECK(fpal_label_valid("Sa\xc4\x9f i\xc5\x9f" "aret")); /* UTF-8 */
	T_CHECK(!fpal_label_valid(NULL));
	T_CHECK(!fpal_label_valid(""));
	T_CHECK(!fpal_label_valid(" leading"));
	T_CHECK(!fpal_label_valid("trailing "));
	T_CHECK(!fpal_label_valid("tab\there"));
	T_CHECK(!fpal_label_valid("line\nbreak"));
	T_CHECK(!fpal_label_valid("bell\a"));
	T_CHECK(fpal_label_valid("123456789012345678901234567890123456789012345678"));
	T_CHECK(!fpal_label_valid("1234567890123456789012345678901234567890123456789"));
}

static void test_label_text_must_be_clean_utf8(void) {
	/* well formed: Turkish, Greek, an emoji (4 bytes) */
	T_CHECK(fpal_label_valid("\xc4\xb0\xc5\x9f" "aret"));
	T_CHECK(fpal_label_valid("\xce\xb1\xce\xb2"));
	T_CHECK(fpal_label_valid("hand \xf0\x9f\x91\x8d"));

	/* broken or smuggled bytes */
	T_CHECK(!fpal_label_valid("\xc0\x80"));		 /* overlong NUL */
	T_CHECK(!fpal_label_valid("\xe0\x80\xaf"));	 /* overlong slash */
	T_CHECK(!fpal_label_valid("\xed\xa0\x80"));	 /* a surrogate */
	T_CHECK(!fpal_label_valid("\xf4\x90\x80\x80")); /* above U+10FFFF */
	T_CHECK(!fpal_label_valid("\xff"));
	T_CHECK(!fpal_label_valid("\x80"));		 /* stray continuation */
	T_CHECK(!fpal_label_valid("ab\xc4"));		 /* ends inside a character */
	T_CHECK(!fpal_label_valid("ab\xe2\x82"));

	/* C1 controls: some terminals act on them (U+009B is CSI) */
	T_CHECK(!fpal_label_valid("a\xc2\x9b" "b"));
	T_CHECK(!fpal_label_valid("a\xc2\x80" "b"));
	T_CHECK(fpal_label_valid("a\xc2\xa0" "b")); /* U+00A0 is a plain no-break space */

	/* invisible or direction changing: a name must read as it looks */
	T_CHECK(!fpal_label_valid("a\xe2\x80\xae" "b")); /* right-to-left override */
	T_CHECK(!fpal_label_valid("a\xe2\x80\x8b" "b")); /* zero width space */
	T_CHECK(!fpal_label_valid("a\xe2\x80\xa8" "b")); /* line separator */
	T_CHECK(!fpal_label_valid("a\xef\xbb\xbf" "b")); /* BOM */
	T_CHECK(!fpal_label_valid("a\xe2\x81\xa6" "b")); /* left-to-right isolate */
}

static void test_label_char_rules(void) {
	T_CHECK(fpal_label_char_ok('a'));
	T_CHECK(fpal_label_char_ok(0x11f));
	T_CHECK(fpal_label_char_ok(0x1f44d));
	T_CHECK(!fpal_label_char_ok(0x1b));
	T_CHECK(!fpal_label_char_ok(0x7f));
	T_CHECK(!fpal_label_char_ok(0x9b));
	T_CHECK(!fpal_label_char_ok(0xd800));
	T_CHECK(!fpal_label_char_ok(0x110000));
	T_CHECK(!fpal_label_char_ok(0x202e));
}

static void test_set_get_replace_remove(void) {
	struct fpal_template_id a, b;
	char out[FPAL_LABEL_MAX + 1];
	char path[128];
	struct stat st;

	make_id(&a, 1);
	make_id(&b, 100);

	T_EQ_INT(fpal_labels_get(dir, &a, out, sizeof(out)), FPAL_OK);
	T_CHECK(out[0] == '\0'); /* no file yet */

	T_EQ_INT(fpal_labels_set(dir, &a, "Windows finger"), FPAL_OK);
	T_EQ_INT(fpal_labels_set(dir, &b, "Linux finger"), FPAL_OK);
	T_EQ_INT(fpal_labels_get(dir, &a, out, sizeof(out)), FPAL_OK);
	T_CHECK(!strcmp(out, "Windows finger"));
	T_EQ_INT(fpal_labels_get(dir, &b, out, sizeof(out)), FPAL_OK);
	T_CHECK(!strcmp(out, "Linux finger"));

	T_EQ_INT(fpal_labels_set(dir, &a, "Right thumb"), FPAL_OK); /* replace */
	T_EQ_INT(fpal_labels_get(dir, &a, out, sizeof(out)), FPAL_OK);
	T_CHECK(!strcmp(out, "Right thumb"));
	T_EQ_INT(fpal_labels_get(dir, &b, out, sizeof(out)), FPAL_OK);
	T_CHECK(!strcmp(out, "Linux finger")); /* the other one is untouched */

	snprintf(path, sizeof(path), "%s/.labels", dir);
	T_EQ_INT(stat(path, &st), 0);
	T_EQ_INT(st.st_mode & 0777, 0600);

	T_EQ_INT(fpal_labels_set(dir, &a, NULL), FPAL_OK); /* remove */
	T_EQ_INT(fpal_labels_get(dir, &a, out, sizeof(out)), FPAL_OK);
	T_CHECK(out[0] == '\0');
	T_EQ_INT(fpal_labels_get(dir, &b, out, sizeof(out)), FPAL_OK);
	T_CHECK(!strcmp(out, "Linux finger"));
	T_EQ_INT(fpal_labels_set(dir, &a, ""), FPAL_OK); /* removing twice is fine */
}

static void test_find_by_label(void) {
	struct fpal_template_id a, b, got;

	make_id(&a, 7);
	make_id(&b, 77);
	T_EQ_INT(fpal_labels_set(dir, &a, "left"), FPAL_OK);
	T_EQ_INT(fpal_labels_set(dir, &b, "right"), FPAL_OK);
	T_EQ_INT(fpal_labels_find(dir, "right", &got), FPAL_OK);
	T_CHECK(fpal_id_equal(&got, &b));
	T_EQ_INT(fpal_labels_find(dir, "nope", &got), FPAL_E_NODATA);
	T_EQ_INT(fpal_labels_set(dir, &a, NULL), FPAL_OK);
	T_EQ_INT(fpal_labels_set(dir, &b, NULL), FPAL_OK);
}

static void test_rejects_bad_input(void) {
	struct fpal_template_id a;
	char out[8];

	make_id(&a, 3);
	T_EQ_INT(fpal_labels_set(dir, &a, "bad\tlabel"), FPAL_E_INVAL);
	T_EQ_INT(fpal_labels_set(dir, NULL, "x"), FPAL_E_INVAL);
	T_EQ_INT(fpal_labels_get(dir, &a, out, 0), FPAL_E_INVAL);

	T_EQ_INT(fpal_labels_set(dir, &a, "a long label here"), FPAL_OK);
	T_EQ_INT(fpal_labels_get(dir, &a, out, sizeof(out)), FPAL_E_OVERFLOW);
	T_EQ_INT(fpal_labels_set(dir, &a, NULL), FPAL_OK);
}

static void test_rejects_symlink_and_loose_mode(void) {
	struct fpal_template_id a;
	char path[128], other[128];
	char out[FPAL_LABEL_MAX + 1];

	make_id(&a, 9);
	snprintf(path, sizeof(path), "%s/.labels", dir);
	snprintf(other, sizeof(other), "%s/elsewhere", dir);

	/* a symlink planted in the state directory is never followed */
	unlink(path);
	T_EQ_INT(symlink(other, path), 0);
	T_EQ_INT(fpal_labels_get(dir, &a, out, sizeof(out)), FPAL_E_PERM);
	unlink(path);

	/* group writable files are not trusted */
	T_EQ_INT(fpal_labels_set(dir, &a, "ok"), FPAL_OK);
	T_EQ_INT(chmod(path, 0664), 0);
	T_EQ_INT(fpal_labels_get(dir, &a, out, sizeof(out)), FPAL_E_PERM);
	unlink(path);
}

static void test_malformed_lines_are_ignored(void) {
	struct fpal_template_id a;
	char path[128], out[FPAL_LABEL_MAX + 1];
	FILE *f;

	make_id(&a, 1);
	snprintf(path, sizeof(path), "%s/.labels", dir);
	f = fopen(path, "w");
	T_CHECK(f != NULL);
	fputs("garbage\n", f);
	fputs("zz\tshort\n", f);
	/* id of make_id(1) = 01 02 03 ... 20, then a valid label */
	for (unsigned i = 0; i < TEST_ID_LEN; i++)
		fprintf(f, "%02x", 1 + i);
	fputs("\tsurvivor\n", f);
	fclose(f);
	chmod(path, 0600);

	T_EQ_INT(fpal_labels_get(dir, &a, out, sizeof(out)), FPAL_OK);
	T_CHECK(!strcmp(out, "survivor"));
	unlink(path);
}

static void test_forget_removes_from_every_user(void) {
	struct fpal_template_id a, b, ids[FPAL_MAX_TEMPLATES];
	size_t n = 0;

	make_id(&a, 20);
	make_id(&b, 200);
	T_EQ_INT(fpal_bindings_add(dir, "alice", &a), FPAL_OK);
	T_EQ_INT(fpal_bindings_add(dir, "alice", &b), FPAL_OK);
	T_EQ_INT(fpal_bindings_add(dir, "bob", &a), FPAL_OK);
	T_EQ_INT(fpal_labels_set(dir, &a, "named"), FPAL_OK); /* .labels must be skipped */

	T_EQ_INT(fpal_bindings_forget(dir, &a), FPAL_OK);
	T_EQ_INT(fpal_bindings_load(dir, "alice", ids, FPAL_MAX_TEMPLATES, &n),
		 FPAL_OK);
	T_EQ_INT(n, 1);
	T_CHECK(fpal_id_equal(&ids[0], &b));
	T_EQ_INT(fpal_bindings_load(dir, "bob", ids, FPAL_MAX_TEMPLATES, &n),
		 FPAL_OK);
	T_EQ_INT(n, 0);

	/* removing an id that is not bound leaves the file alone */
	T_EQ_INT(fpal_bindings_remove(dir, "alice", &a), FPAL_OK);
	T_CHECK(fpal_bindings_contains(dir, "alice", &b));

	fpal_bindings_clear(dir, "alice");
	fpal_bindings_clear(dir, "bob");
	fpal_labels_set(dir, &a, NULL);
}

int main(void) {
	char tmpl[] = "/tmp/fpal-labels-XXXXXX";

	if (!mkdtemp(tmpl)) {
		perror("mkdtemp");
		return 1;
	}
	snprintf(dir, sizeof(dir), "%s", tmpl);

	T_RUN(test_valid_labels);
	T_RUN(test_label_text_must_be_clean_utf8);
	T_RUN(test_label_char_rules);
	T_RUN(test_set_get_replace_remove);
	T_RUN(test_find_by_label);
	T_RUN(test_rejects_bad_input);
	T_RUN(test_rejects_symlink_and_loose_mode);
	T_RUN(test_malformed_lines_are_ignored);
	T_RUN(test_forget_removes_from_every_user);

	{
		char cmd[96];

		snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpl);
		if (system(cmd) != 0)
			fputs("cleanup failed\n", stderr);
	}
	return T_DONE();
}
