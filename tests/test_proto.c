// SPDX-License-Identifier: Apache-2.0
/*
 * Byte exact tests for the Egis MoC framing and payload builders.
 * Expected checksums were computed by hand from the algorithm description.
 */
#include "core/bytes.h"
#include "drivers/egis_moc/egis_moc_proto.h"
#include "mock_transport.h"
#include "id_util.h"
#include "t.h"

static void test_frame_sensor_reset(void) {
	static const uint8_t expected[] = { 'E',  'G',	'I',  'S',  0x00, 0x00,
					    0x00, 0x01, 0x21, 0x46, 0x00, 0x00,
					    0x00, 0x04, 0x50, 0x1a, 0x00, 0x00 };
	uint8_t out[64];
	size_t len = 0;

	T_EQ_INT(egis_frame(egis_cmd_sensor_reset.data,
			    egis_cmd_sensor_reset.len, out, sizeof(out), &len),
		 FPAL_OK);
	T_EQ_INT(len, sizeof(expected));
	T_EQ_MEM(out, expected, sizeof(expected));
}

/* Odd frame length: the implicit pad byte takes part in the checksum. */
static void test_frame_fw_version_odd_length(void) {
	uint8_t out[64];
	size_t len = 0;

	T_EQ_INT(egis_frame(egis_cmd_fw_version.data, egis_cmd_fw_version.len,
			    out, sizeof(out), &len),
		 FPAL_OK);
	T_EQ_INT(len, 21);
	T_EQ_INT(fpal_get_be16(out + EGIS_MAGIC_LEN), 0x14de);
}

static void test_frame_list(void) {
	uint8_t out[64];
	size_t len = 0;

	T_EQ_INT(egis_frame(egis_cmd_list.data, egis_cmd_list.len, out,
			    sizeof(out), &len),
		 FPAL_OK);
	T_EQ_INT(fpal_get_be16(out + EGIS_MAGIC_LEN), 0xdd42);
}

/* Property: a framed command always sums to 0 mod 0xFFFF. */
static void test_checksum_self_verifies(void) {
	const struct egis_cmd *cmds[] = { &egis_cmd_fw_version, &egis_cmd_list,
					  &egis_cmd_sensor_reset,
					  &egis_cmd_sensor_check,
					  &egis_cmd_arm_identify };

	for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
		uint8_t out[64];
		size_t len = 0;
		uint32_t sum = 0;

		egis_frame(cmds[i]->data, cmds[i]->len, out, sizeof(out), &len);
		for (size_t j = 0; j < len; j += 2)
			sum += j + 1 < len ? fpal_get_be16(out + j) :
					     (uint32_t)out[j] << 8;
		T_EQ_INT(sum % 0xffffu, 0);
	}
}

static void test_frame_overflow(void) {
	uint8_t out[8];
	size_t len;

	T_EQ_INT(egis_frame(egis_cmd_list.data, egis_cmd_list.len, out,
			    sizeof(out), &len),
		 FPAL_E_OVERFLOW);
}

static void test_build_check_one_id(void) {
	struct fpal_template_id id;
	uint8_t out[256];
	size_t len = 0;

	id_fill(&id, 0xab, EGIS_ID_LEN);
	T_EQ_INT(egis_build_check(&id, 1, 0, out, sizeof(out), &len), FPAL_OK);

	/* 4 len + 5 prefix + 2 len + 32 separator + 32 id + 2 suffix */
	T_EQ_INT(len, 77);
	T_EQ_INT(fpal_get_be32(out), 0x49);
	T_EQ_MEM(out + 4, "\x50\x17\x03\x00\x00", 5);
	T_EQ_INT(fpal_get_be16(out + 9), 0x40);
	for (size_t i = 11; i < 43; i++)
		T_EQ_INT(out[i], 0);
	T_EQ_MEM(out + 43, fpal_id_data(&id), 32);
	T_EQ_MEM(out + 75, "\x00\x40", 2);
}

/* Seven ids push both length fields past one byte (libfprint's > 6 case). */
static void test_build_check_seven_ids_type2(void) {
	struct fpal_template_id ids[7];
	uint8_t out[512];
	size_t len = 0;

	for (size_t i = 0; i < 7; i++)
		id_fill(&ids[i], 0x11, TEST_ID_LEN);
	T_EQ_INT(egis_build_check(ids, 7, EGIS_FLAG_CHECK_PREFIX_TYPE2, out,
				  sizeof(out), &len),
		 FPAL_OK);
	T_EQ_INT(fpal_get_be32(out), 0x0109);
	T_EQ_MEM(out + 4, "\x50\x17\x03\x80\x00", 5);
	T_EQ_INT(fpal_get_be16(out + 9), 0x0100);
	T_EQ_INT(len, 4 + 0x0109);
}

static void test_build_check_too_many(void) {
	struct fpal_template_id ids[EGIS_MAX_TEMPLATES + 1];
	uint8_t out[1024];
	size_t len;

	memset(ids, 0, sizeof(ids));
	T_EQ_INT(egis_build_check(ids, EGIS_MAX_TEMPLATES + 1, 0, out,
				  sizeof(out), &len),
		 FPAL_E_INVAL);
}

static void test_parse_ok(void) {
	struct mock m;
	struct egis_rsp rsp;

	mock_init(&m);
	mock_push_egis(&m, (const uint8_t *)"1.2.3", 5, EGIS_SW_OK);

	T_EQ_INT(egis_parse(m.in[0].data, m.in[0].len, &rsp), FPAL_OK);
	T_EQ_INT(rsp.sw, EGIS_SW_OK);
	T_EQ_INT(rsp.body_len, 5);
	T_EQ_MEM(rsp.body, "1.2.3", 5);
	T_CHECK(rsp.checksum_ok);
}

static void test_parse_rejects_bad_magic(void) {
	struct mock m;
	struct egis_rsp rsp;

	mock_init(&m);
	mock_push_egis(&m, NULL, 0, EGIS_SW_OK);
	m.in[0].data[0] = 'X';
	T_EQ_INT(egis_parse(m.in[0].data, m.in[0].len, &rsp), FPAL_E_PROTO);
}

static void test_parse_rejects_truncated(void) {
	struct mock m;
	struct egis_rsp rsp;

	mock_init(&m);
	mock_push_egis(&m, (const uint8_t *)"abcdef", 6, EGIS_SW_OK);
	/* Length field promises more bytes than were received. */
	T_EQ_INT(egis_parse(m.in[0].data, m.in[0].len - 3, &rsp),
		 FPAL_E_PROTO);
	T_EQ_INT(egis_parse(m.in[0].data, 10, &rsp), FPAL_E_PROTO);
}

static void test_parse_list(void) {
	uint8_t body[2 * EGIS_ID_LEN];
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	struct egis_rsp rsp;
	struct mock m;
	size_t n = 0;

	memset(body, 0x01, 32);
	memset(body + 32, 0x02, 32);
	mock_init(&m);
	mock_push_egis(&m, body, sizeof(body), EGIS_SW_OK);

	T_EQ_INT(egis_parse(m.in[0].data, m.in[0].len, &rsp), FPAL_OK);
	T_EQ_INT(egis_parse_list(&rsp, ids, FPAL_MAX_TEMPLATES, &n), FPAL_OK);
	T_EQ_INT(n, 2);
	T_EQ_MEM(fpal_id_data(&ids[0]), body, 32);
	T_EQ_MEM(fpal_id_data(&ids[1]), body + 32, 32);

	T_EQ_INT(egis_parse_list(&rsp, ids, 1, &n), FPAL_E_OVERFLOW);
}

/*
 * Frames captured from a real 1c7a:0587 (fw 9050.6.4.67). Frames carrying
 * template ids are left out on purpose.
 */
static void test_real_capture_responses(void) {
	static const uint8_t fw[] = { 0x53, 0x49, 0x47, 0x45, 0x00, 0x00, 0x00,
				      0x01, 0x34, 0xd2, 0x00, 0x00, 0x00, 0x0d,
				      '9',  '0',  '5',	'0',  '.',  '6',  '.',
				      '4',  '.',  '6',	'7',  0x90, 0x00 };
	static const uint8_t reset_ok[] = { 0x53, 0x49, 0x47, 0x45, 0x00, 0x00,
					    0x00, 0x01, 0xd5, 0x6d, 0x00, 0x00,
					    0x00, 0x02, 0x90, 0x00 };
	/* "arm identify" answers 91 00 rather than 90 00 */
	static const uint8_t arm_rsp[] = { 0x53, 0x49, 0x47, 0x45, 0x00, 0x00,
					   0x00, 0x01, 0xd4, 0x6d, 0x00, 0x00,
					   0x00, 0x02, 0x91, 0x00 };
	struct egis_rsp rsp;

	T_EQ_INT(egis_parse(fw, sizeof(fw), &rsp), FPAL_OK);
	T_CHECK(rsp.checksum_ok);
	T_EQ_INT(rsp.sw, EGIS_SW_OK);
	T_EQ_INT(rsp.body_len, 11);
	T_EQ_MEM(rsp.body, "9050.6.4.67", 11);

	T_EQ_INT(egis_parse(reset_ok, sizeof(reset_ok), &rsp), FPAL_OK);
	T_CHECK(rsp.checksum_ok);
	T_EQ_INT(rsp.sw, EGIS_SW_OK);
	T_EQ_INT(rsp.body_len, 0);

	T_EQ_INT(egis_parse(arm_rsp, sizeof(arm_rsp), &rsp), FPAL_OK);
	T_CHECK(rsp.checksum_ok);
	T_EQ_INT(rsp.sw, 0x9100);
}

/* Command checksums the real sensor accepted. */
static void test_real_capture_commands(void) {
	uint8_t out[64];
	size_t len = 0;

	egis_frame(egis_cmd_arm_identify.data, egis_cmd_arm_identify.len, out,
		   sizeof(out), &len);
	T_EQ_INT(fpal_get_be16(out + EGIS_MAGIC_LEN), 0x2048);
	egis_frame(egis_cmd_sensor_check.data, egis_cmd_sensor_check.len, out,
		   sizeof(out), &len);
	T_EQ_INT(fpal_get_be16(out + EGIS_MAGIC_LEN), 0x1f49);
}

int main(void) {
	T_RUN(test_frame_sensor_reset);
	T_RUN(test_frame_fw_version_odd_length);
	T_RUN(test_frame_list);
	T_RUN(test_checksum_self_verifies);
	T_RUN(test_frame_overflow);
	T_RUN(test_build_check_one_id);
	T_RUN(test_build_check_seven_ids_type2);
	T_RUN(test_build_check_too_many);
	T_RUN(test_parse_ok);
	T_RUN(test_parse_rejects_bad_magic);
	T_RUN(test_parse_rejects_truncated);
	T_RUN(test_parse_list);
	T_RUN(test_real_capture_responses);
	T_RUN(test_real_capture_commands);
	return T_DONE();
}
