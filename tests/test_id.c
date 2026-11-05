// SPDX-License-Identifier: Apache-2.0
/*
 * Template id helpers. Written against the interface (length, data, hex), not
 * against how an id is stored, so they hold before and after ids became
 * variable length.
 */
#include <string.h>

#include "fpal/id.h"
#include "t.h"

#define LEN FPAL_TEMPLATE_ID_LEN

static struct fpal_template_id make(uint8_t seed) {
	struct fpal_template_id id;
	uint8_t raw[LEN];

	for (size_t i = 0; i < LEN; i++)
		raw[i] = (uint8_t)(seed + i);
	T_EQ_INT(fpal_id_set(&id, raw, LEN), FPAL_OK);
	return id;
}

static void test_set_and_read_back(void) {
	struct fpal_template_id id = make(0x10);

	T_EQ_INT(fpal_id_len(&id), LEN);
	T_EQ_INT(fpal_id_data(&id)[0], 0x10);
	T_EQ_INT(fpal_id_data(&id)[LEN - 1], 0x10 + LEN - 1);
}

static void test_set_rejects_bad_input(void) {
	struct fpal_template_id id;
	uint8_t raw[300] = { 0 };

	T_EQ_INT(fpal_id_set(&id, raw, 0), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_set(&id, raw, sizeof(raw)), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_set(&id, NULL, LEN), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_set(NULL, raw, LEN), FPAL_E_INVAL);
}

static void test_equal(void) {
	struct fpal_template_id a = make(1), b = make(1), c = make(2);

	T_CHECK(fpal_id_equal(&a, &b));
	T_CHECK(!fpal_id_equal(&a, &c));
	/* one differing byte at the very end must be noticed */
	b = a;
	((uint8_t *)(uintptr_t)fpal_id_data(&b))[LEN - 1] ^= 1;
	T_CHECK(!fpal_id_equal(&a, &b));
}

static void test_prefix(void) {
	struct fpal_template_id id;

	T_EQ_INT(fpal_id_set(&id, "FPAL-7b516a3ef8dab92ad49f9124678", LEN), FPAL_OK);
	T_CHECK(fpal_id_has_prefix(&id, "FPAL-"));
	T_CHECK(fpal_id_has_prefix(&id, "FPAL-7b51"));
	T_CHECK(fpal_id_has_prefix(&id, ""));
	T_CHECK(!fpal_id_has_prefix(&id, "FPAL-8"));
	T_CHECK(!fpal_id_has_prefix(&id, "fpal-"));
	T_CHECK(!fpal_id_has_prefix(&id, "FPAL-7b516a3ef8dab92ad49f9124678-and-more"));
}

static void test_hex_round_trip(void) {
	struct fpal_template_id id = make(0xf0), back;
	char hex[2 * LEN + 1];

	T_EQ_INT(fpal_id_to_hex(&id, hex, sizeof(hex)), FPAL_OK);
	T_EQ_INT(strlen(hex), 2 * LEN);
	T_CHECK(!strncmp(hex, "f0f1f2f3", 8)); /* lower case */
	T_EQ_INT(fpal_id_from_hex(hex, strlen(hex), false, &back), FPAL_OK);
	T_CHECK(fpal_id_equal(&id, &back));
	T_EQ_INT(fpal_id_to_hex(&id, hex, sizeof(hex) - 1), FPAL_E_OVERFLOW);
}

static void test_hex_parsing_is_strict(void) {
	struct fpal_template_id id;
	char hex[2 * LEN + 2];

	memset(hex, 'a', sizeof(hex));
	T_EQ_INT(fpal_id_from_hex(hex, 2 * LEN, false, &id), FPAL_OK);
	T_EQ_INT(fpal_id_from_hex(hex, 2 * LEN - 1, false, &id), FPAL_E_INVAL); /* odd */
	T_EQ_INT(fpal_id_from_hex(hex, 2 * LEN + 2, false, &id), FPAL_E_INVAL); /* too long */
	T_EQ_INT(fpal_id_from_hex(hex, 0, false, &id), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_from_hex(NULL, 2 * LEN, false, &id), FPAL_E_INVAL);

	hex[5] = 'g';
	T_EQ_INT(fpal_id_from_hex(hex, 2 * LEN, false, &id), FPAL_E_INVAL);
	hex[5] = ' ';
	T_EQ_INT(fpal_id_from_hex(hex, 2 * LEN, false, &id), FPAL_E_INVAL);
}

static void test_hex_case(void) {
	struct fpal_template_id lower, upper;
	char hex[2 * LEN + 1];

	memset(hex, 'A', 2 * LEN);
	hex[2 * LEN] = '\0';
	T_EQ_INT(fpal_id_from_hex(hex, 2 * LEN, false, &upper), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_from_hex(hex, 2 * LEN, true, &upper), FPAL_OK);
	memset(hex, 'a', 2 * LEN);
	T_EQ_INT(fpal_id_from_hex(hex, 2 * LEN, false, &lower), FPAL_OK);
	T_CHECK(fpal_id_equal(&lower, &upper)); /* same value either way */
}

static void test_text_is_printable(void) {
	struct fpal_template_id id = make(0x1e); /* 0x1e 0x1f 0x20 0x21 ... */
	char text[LEN + 1], small[4];

	fpal_id_text(&id, text, sizeof(text));
	T_EQ_INT(strlen(text), LEN);
	for (size_t i = 0; i < LEN; i++)
		T_CHECK((unsigned char)text[i] >= 0x20 && (unsigned char)text[i] <= 0x7e);
	T_EQ_INT(text[0], '.'); /* 0x1e is a control character */
	T_EQ_INT(text[2], ' ');	/* 0x20 */
	T_EQ_INT(text[3], '!');	/* 0x21 */

	fpal_id_text(&id, small, sizeof(small)); /* truncated, still terminated */
	T_EQ_INT(strlen(small), 3);
	fpal_id_text(&id, small, 0); /* nothing to write into: no crash */
}

int main(void) {
	T_RUN(test_set_and_read_back);
	T_RUN(test_set_rejects_bad_input);
	T_RUN(test_equal);
	T_RUN(test_prefix);
	T_RUN(test_hex_round_trip);
	T_RUN(test_hex_parsing_is_strict);
	T_RUN(test_hex_case);
	T_RUN(test_text_is_printable);
	return T_DONE();
}
