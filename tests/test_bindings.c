// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * Binding file tests, run in a private temporary directory.
 */
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fpx/bindings.h"
#include "t.h"

static char dir[64];

static void make_id(struct fpx_template_id *id, uint8_t seed)
{
	for (size_t i = 0; i < FPX_TEMPLATE_ID_LEN; i++)
		id->bytes[i] = (uint8_t)(seed + i);
}

static void test_valid_user(void)
{
	T_CHECK(fpx_bindings_valid_user("ohnando"));
	T_CHECK(fpx_bindings_valid_user("user_1.name-x"));
	T_CHECK(!fpx_bindings_valid_user(""));
	T_CHECK(!fpx_bindings_valid_user(NULL));
	T_CHECK(!fpx_bindings_valid_user("../etc/passwd"));
	T_CHECK(!fpx_bindings_valid_user("a/b"));
	T_CHECK(!fpx_bindings_valid_user(".hidden"));
	T_CHECK(!fpx_bindings_valid_user("-rf"));
	T_CHECK(!fpx_bindings_valid_user("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")); /* 33 */
}

static void test_missing_file_is_empty(void)
{
	struct fpx_template_id ids[FPX_MAX_TEMPLATES];
	size_t n = 99;

	T_EQ_INT(fpx_bindings_load(dir, "nobody", ids, FPX_MAX_TEMPLATES, &n),
		 FPX_OK);
	T_EQ_INT(n, 0);
}

static void test_add_load_roundtrip(void)
{
	struct fpx_template_id a, b, ids[FPX_MAX_TEMPLATES];
	char path[128];
	struct stat st;
	size_t n = 0;

	make_id(&a, 0x10);
	make_id(&b, 0xf0);

	T_EQ_INT(fpx_bindings_add(dir, "alice", &a), FPX_OK);
	T_EQ_INT(fpx_bindings_add(dir, "alice", &b), FPX_OK);
	T_EQ_INT(fpx_bindings_add(dir, "alice", &a), FPX_OK); /* duplicate */

	T_EQ_INT(fpx_bindings_load(dir, "alice", ids, FPX_MAX_TEMPLATES, &n),
		 FPX_OK);
	T_EQ_INT(n, 2);
	T_EQ_MEM(ids[0].bytes, a.bytes, FPX_TEMPLATE_ID_LEN);
	T_EQ_MEM(ids[1].bytes, b.bytes, FPX_TEMPLATE_ID_LEN);

	T_CHECK(fpx_bindings_contains(dir, "alice", &b));
	T_CHECK(!fpx_bindings_contains(dir, "bob", &b));

	snprintf(path, sizeof(path), "%s/alice", dir);
	T_EQ_INT(stat(path, &st), 0);
	T_EQ_INT(st.st_mode & 0777, 0600);
}

static void test_clear(void)
{
	struct fpx_template_id a;

	make_id(&a, 0x33);
	T_EQ_INT(fpx_bindings_add(dir, "carol", &a), FPX_OK);
	T_EQ_INT(fpx_bindings_clear(dir, "carol"), FPX_OK);
	T_CHECK(!fpx_bindings_contains(dir, "carol", &a));
	T_EQ_INT(fpx_bindings_clear(dir, "carol"), FPX_OK); /* idempotent */
}

static void test_rejects_symlink_and_loose_mode(void)
{
	struct fpx_template_id a, ids[FPX_MAX_TEMPLATES];
	char path[128];
	size_t n;

	make_id(&a, 0x44);
	T_EQ_INT(fpx_bindings_add(dir, "dave", &a), FPX_OK);
	snprintf(path, sizeof(path), "%s/dave", dir);

	chmod(path, 0666);
	T_EQ_INT(fpx_bindings_load(dir, "dave", ids, FPX_MAX_TEMPLATES, &n),
		 FPX_E_PERM);
	T_CHECK(!fpx_bindings_contains(dir, "dave", &a));

	snprintf(path, sizeof(path), "%s/eve", dir);
	T_EQ_INT(symlink("dave", path), 0);
	T_EQ_INT(fpx_bindings_load(dir, "eve", ids, FPX_MAX_TEMPLATES, &n),
		 FPX_E_IO);
}

static void test_rejects_bad_user(void)
{
	struct fpx_template_id a;

	make_id(&a, 0);
	T_EQ_INT(fpx_bindings_add(dir, "../escape", &a), FPX_E_INVAL);
}

int main(void)
{
	int rc;

	snprintf(dir, sizeof(dir), "/tmp/fpx-test-XXXXXX");
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 1;
	}

	T_RUN(test_valid_user);
	T_RUN(test_missing_file_is_empty);
	T_RUN(test_add_load_roundtrip);
	T_RUN(test_clear);
	T_RUN(test_rejects_symlink_and_loose_mode);
	T_RUN(test_rejects_bad_user);
	rc = T_DONE();

	{
		char cmd[96];

		snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
		if (system(cmd) != 0)
			fprintf(stderr, "could not remove %s\n", dir);
	}
	return rc;
}
