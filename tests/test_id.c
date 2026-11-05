// SPDX-License-Identifier: Apache-2.0
/*
 * Template id helpers: ids of any length from 1 to FPAL_ID_MAX bytes.
 */
#include <string.h>

#include "id_util.h"
#include "t.h"

static const size_t lengths[] = { 1, 2, 16, 20, 32, 100, FPAL_ID_MAX };

static void test_set_and_read_back(void) {
	for (size_t k = 0; k < sizeof(lengths) / sizeof(lengths[0]); k++) {
		struct fpal_template_id id;

		id_seq(&id, 0x10, lengths[k]);
		T_EQ_INT(fpal_id_len(&id), lengths[k]);
		T_EQ_INT(fpal_id_data(&id)[0], 0x10);
		T_EQ_INT(fpal_id_data(&id)[lengths[k] - 1],
			 (uint8_t)(0x10 + lengths[k] - 1));
	}
}

static void test_set_rejects_bad_input(void) {
	struct fpal_template_id id;
	uint8_t raw[FPAL_ID_MAX + 1] = { 0 };

	T_EQ_INT(fpal_id_set(&id, raw, 0), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_set(&id, raw, FPAL_ID_MAX + 1), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_set(&id, NULL, 4), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_set(NULL, raw, 4), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_set(&id, raw, FPAL_ID_MAX), FPAL_OK); /* the limit itself */
}

static void test_a_damaged_struct_cannot_make_us_read_past_it(void) {
	struct fpal_template_id id;

	memset(&id, 0xff, sizeof(id)); /* len 255 */
	T_CHECK(fpal_id_len(&id) <= FPAL_ID_MAX);
}

static void test_equal_needs_same_length_and_bytes(void) {
	struct fpal_template_id a, b, c;

	id_seq(&a, 1, 20);
	id_seq(&b, 1, 20);
	id_seq(&c, 1, 21);
	T_CHECK(fpal_id_equal(&a, &b));
	T_CHECK(!fpal_id_equal(&a, &c)); /* a prefix is not the same id */
	T_CHECK(!fpal_id_equal(&c, &a));
	b = a;
	b.data[19] ^= 1; /* the very last byte */
	T_CHECK(!fpal_id_equal(&a, &b));
	b = a;
	b.data[25] ^= 1; /* beyond the length: not part of the id */
	T_CHECK(fpal_id_equal(&a, &b));
}

static void test_prefix(void) {
	struct fpal_template_id id;

	id_pad(&id, "FPAL-7b516a3ef8dab92ad49f9124678", 32);
	T_CHECK(fpal_id_has_prefix(&id, "FPAL-"));
	T_CHECK(fpal_id_has_prefix(&id, "FPAL-7b51"));
	T_CHECK(fpal_id_has_prefix(&id, ""));
	T_CHECK(!fpal_id_has_prefix(&id, "FPAL-8"));
	T_CHECK(!fpal_id_has_prefix(&id, "fpal-"));
	T_CHECK(!fpal_id_has_prefix(&id, "FPAL-7b516a3ef8dab92ad49f9124678-and-more"));

	id_pad(&id, "FP", 2); /* shorter than the prefix */
	T_CHECK(!fpal_id_has_prefix(&id, "FPAL-"));
	T_CHECK(fpal_id_has_prefix(&id, "FP"));
}

static void test_hex_round_trip_for_every_length(void) {
	for (size_t k = 0; k < sizeof(lengths) / sizeof(lengths[0]); k++) {
		struct fpal_template_id id, back;
		char hex[FPAL_ID_HEX_MAX + 1];

		id_seq(&id, 0xf0, lengths[k]);
		T_EQ_INT(fpal_id_to_hex(&id, hex, sizeof(hex)), FPAL_OK);
		T_EQ_INT(strlen(hex), 2 * lengths[k]);
		T_CHECK(!strncmp(hex, "f0f1", lengths[k] > 1 ? 4 : 2) ||
			lengths[k] == 1); /* lower case */
		T_EQ_INT(fpal_id_from_hex(hex, strlen(hex), false, &back), FPAL_OK);
		T_CHECK(fpal_id_equal(&id, &back));
		T_EQ_INT(fpal_id_to_hex(&id, hex, 2 * lengths[k]), FPAL_E_OVERFLOW);
	}
}

static void test_hex_parsing_is_strict(void) {
	struct fpal_template_id id;
	char hex[FPAL_ID_HEX_MAX + 8];

	memset(hex, 'a', sizeof(hex));
	T_EQ_INT(fpal_id_from_hex(hex, 2, false, &id), FPAL_OK); /* one byte */
	T_EQ_INT(fpal_id_from_hex(hex, FPAL_ID_HEX_MAX, false, &id), FPAL_OK);
	T_EQ_INT(fpal_id_from_hex(hex, FPAL_ID_HEX_MAX + 2, false, &id), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_from_hex(hex, 63, false, &id), FPAL_E_INVAL); /* odd */
	T_EQ_INT(fpal_id_from_hex(hex, 1, false, &id), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_from_hex(hex, 0, false, &id), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_from_hex(NULL, 64, false, &id), FPAL_E_INVAL);

	hex[5] = 'g';
	T_EQ_INT(fpal_id_from_hex(hex, 64, false, &id), FPAL_E_INVAL);
	hex[5] = ' ';
	T_EQ_INT(fpal_id_from_hex(hex, 64, false, &id), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_from_hex(hex, 4, false, &id), FPAL_OK); /* before the bad digit */
}

static void test_hex_case(void) {
	struct fpal_template_id lower, upper;

	T_EQ_INT(fpal_id_from_hex("ABCDEF", 6, false, &upper), FPAL_E_INVAL);
	T_EQ_INT(fpal_id_from_hex("ABCDEF", 6, true, &upper), FPAL_OK);
	T_EQ_INT(fpal_id_from_hex("abcdef", 6, false, &lower), FPAL_OK);
	T_CHECK(fpal_id_equal(&lower, &upper)); /* same value either way */
}

static void test_text_is_printable(void) {
	struct fpal_template_id id;
	char text[FPAL_ID_MAX + 1], small[4];

	id_seq(&id, 0x1e, 32); /* 0x1e 0x1f 0x20 0x21 ... */
	fpal_id_text(&id, text, sizeof(text));
	T_EQ_INT(strlen(text), 32);
	for (size_t i = 0; i < 32; i++)
		T_CHECK((unsigned char)text[i] >= 0x20 && (unsigned char)text[i] <= 0x7e);
	T_EQ_INT(text[0], '.'); /* 0x1e is a control character */
	T_EQ_INT(text[2], ' ');	/* 0x20 */
	T_EQ_INT(text[3], '!');	/* 0x21 */

	fpal_id_text(&id, small, sizeof(small)); /* truncated, still terminated */
	T_EQ_INT(strlen(small), 3);
	fpal_id_text(&id, small, 0); /* nothing to write into: no crash */

	id_seq(&id, 0x41, 5); /* a short id gives a short text */
	fpal_id_text(&id, text, sizeof(text));
	T_CHECK(!strcmp(text, "ABCDE"));
}

int main(void) {
	T_RUN(test_set_and_read_back);
	T_RUN(test_set_rejects_bad_input);
	T_RUN(test_a_damaged_struct_cannot_make_us_read_past_it);
	T_RUN(test_equal_needs_same_length_and_bytes);
	T_RUN(test_prefix);
	T_RUN(test_hex_round_trip_for_every_length);
	T_RUN(test_hex_parsing_is_strict);
	T_RUN(test_hex_case);
	T_RUN(test_text_is_printable);
	return T_DONE();
}
