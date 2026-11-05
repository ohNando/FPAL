// SPDX-License-Identifier: Apache-2.0
/*
 * Picking a finger from the list. Deleting depends on this never choosing
 * the wrong template, so every ambiguous or odd input must be refused.
 */
#include <stdlib.h>
#include <unistd.h>

#include "../tools/finger.h"
#include "fpal/labels.h"
#include "id_util.h"
#include "t.h"

static char dir[64];
static struct fpal_template_id ids[3];

static void setup(void) {
	/* [0] binary like a Windows id, [1] and [2] made by FPAL */
	uint8_t raw[TEST_ID_LEN];

	for (size_t i = 0; i < sizeof(raw); i++)
		raw[i] = (uint8_t)(0x7d + i * 7);
	(void)fpal_id_set(&ids[0], raw, sizeof(raw));
	id_pad(&ids[1], "FPAL-7b516a3ef8dab92ad49f9124678", TEST_ID_LEN);
	id_pad(&ids[2], "FPAL-7b99ffffffffffffffffffffff0", TEST_ID_LEN);
	fpal_labels_set(dir, &ids[0], "Windows finger");
	fpal_labels_set(dir, &ids[1], "Linux finger");
}

static size_t pick(const char *arg, int *rc, size_t *matches) {
	size_t idx = 99;

	*rc = fpal_finger_find(dir, ids, 3, arg, &idx, matches);
	return idx;
}

static void test_by_number(void) {
	size_t m;
	int rc;

	T_EQ_INT(pick("0", &rc, &m), 0);
	T_EQ_INT(rc, FPAL_OK);
	T_EQ_INT(pick("2", &rc, &m), 2);
	T_EQ_INT(rc, FPAL_OK);
	pick("3", &rc, &m); /* the list has 0..2 */
	T_EQ_INT(rc, FPAL_E_NODATA);
	pick("99", &rc, &m);
	T_EQ_INT(rc, FPAL_E_NODATA);
}

static void test_by_name(void) {
	size_t m;
	int rc;

	T_EQ_INT(pick("Linux finger", &rc, &m), 1);
	T_EQ_INT(rc, FPAL_OK);
	T_EQ_INT(pick("Windows finger", &rc, &m), 0);
	pick("linux finger", &rc, &m); /* exact, case sensitive */
	T_EQ_INT(rc, FPAL_E_NODATA);
	pick("Linux", &rc, &m); /* no partial names */
	T_EQ_INT(rc, FPAL_E_NODATA);
}

static void test_by_id_prefix(void) {
	size_t m;
	int rc;

	T_EQ_INT(pick("FPAL-7b51", &rc, &m), 1);
	T_EQ_INT(rc, FPAL_OK);
	T_EQ_INT(m, 1);
	T_EQ_INT(pick("FPAL-7b99", &rc, &m), 2);
	pick("7d", &rc, &m); /* not a number, too short to be an id prefix */
	T_EQ_INT(rc, FPAL_E_NODATA);
	pick("7d84", &rc, &m);
	T_EQ_INT(rc, FPAL_E_NODATA);
}

static void test_ambiguous_is_refused(void) {
	size_t m;
	int rc;

	pick("FPAL-7b", &rc, &m); /* starts both FPAL ids */
	T_EQ_INT(rc, FPAL_E_EXISTS);
	T_EQ_INT(m, 2);
}

static void test_hex_prefix(void) {
	size_t m;
	int rc;

	/* "FPAL-7b5" is 46 50 41 4c 2d 37 62 35: only ids[1] starts that way */
	T_EQ_INT(pick("4650414c2d376235", &rc, &m), 1);
	T_EQ_INT(rc, FPAL_OK);
	/* "FPAL-7b" starts both FPAL ids */
	pick("4650414c2d3762", &rc, &m);
	T_EQ_INT(rc, FPAL_E_EXISTS);
	T_EQ_INT(m, 2);
	pick("4650414c2d376236", &rc, &m); /* matches nothing */
	T_EQ_INT(rc, FPAL_E_NODATA);
	pick("4650414c", &rc, &m); /* 8 digits, still both */
	T_EQ_INT(rc, FPAL_E_EXISTS);
}

static void test_garbage_is_refused(void) {
	size_t m;
	int rc;

	pick("", &rc, &m);
	T_EQ_INT(rc, FPAL_E_NODATA);
	pick("--clear", &rc, &m);
	T_EQ_INT(rc, FPAL_E_NODATA);
	pick("FPAL", &rc, &m); /* shorter than 6 characters */
	T_EQ_INT(rc, FPAL_E_NODATA);
}

int main(void) {
	char tmpl[] = "/tmp/fpal-finger-XXXXXX";

	if (!mkdtemp(tmpl)) {
		perror("mkdtemp");
		return 1;
	}
	snprintf(dir, sizeof(dir), "%s", tmpl);
	setup();

	T_RUN(test_by_number);
	T_RUN(test_by_name);
	T_RUN(test_by_id_prefix);
	T_RUN(test_ambiguous_is_refused);
	T_RUN(test_hex_prefix);
	T_RUN(test_garbage_is_refused);

	{
		char cmd[96];

		snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpl);
		if (system(cmd) != 0)
			fputs("cleanup failed\n", stderr);
	}
	return T_DONE();
}
