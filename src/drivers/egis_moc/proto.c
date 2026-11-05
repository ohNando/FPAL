// SPDX-License-Identifier: Apache-2.0
/*
 * Egis MoC framing, checksum and payload builders. Pure functions without
 * I/O so they can be unit tested byte for byte.
 */
#include <string.h>

#include "core/bytes.h"
#include "core/log.h"
#include "egis_moc_proto.h"

static const uint8_t write_magic[EGIS_MAGIC_LEN] = { 'E', 'G', 'I', 'S',
						     0x00, 0x00, 0x00, 0x01 };
static const uint8_t read_magic[EGIS_MAGIC_LEN] = { 'S', 'I', 'G', 'E',
						    0x00, 0x00, 0x00, 0x01 };

static const uint8_t fw_version[] = { 0x00, 0x00, 0x00, 0x07, 0x50, 0x7f,
				      0x00, 0x00, 0x00, 0x00, 0x0c };
static const uint8_t list[] = { 0x00, 0x00, 0x00, 0x07, 0x50, 0x19,
				0x04, 0x00, 0x00, 0x01, 0x40 };
static const uint8_t sensor_reset[] = { 0x00, 0x00, 0x00, 0x04,
					0x50, 0x1a, 0x00, 0x00 };
static const uint8_t sensor_check[] = { 0x00, 0x00, 0x00, 0x04,
					0x50, 0x17, 0x02, 0x00 };
static const uint8_t arm_identify[] = { 0x00, 0x00, 0x00, 0x04,
					0x50, 0x17, 0x01, 0x01 };

const struct egis_cmd egis_cmd_fw_version = { fw_version, sizeof(fw_version) };
const struct egis_cmd egis_cmd_list = { list, sizeof(list) };
const struct egis_cmd egis_cmd_sensor_reset = { sensor_reset,
						sizeof(sensor_reset) };
const struct egis_cmd egis_cmd_sensor_check = { sensor_check,
						sizeof(sensor_check) };
const struct egis_cmd egis_cmd_arm_identify = { arm_identify,
						sizeof(arm_identify) };

#ifdef FPAL_ENABLE_ENROLL
static const uint8_t arm_enroll[] = { 0x00, 0x00, 0x00, 0x04,
				      0x50, 0x17, 0x01, 0x00 };
static const uint8_t enroll_start[] = { 0x00, 0x00, 0x00, 0x07, 0x50, 0x16,
					0x01, 0x00, 0x00, 0x00, 0x20 };
static const uint8_t start_capture[] = { 0x00, 0x00, 0x00, 0x04,
					 0x50, 0x16, 0x02, 0x01 };
static const uint8_t read_capture[] = { 0x00, 0x00, 0x00, 0x07, 0x50, 0x16,
					0x02, 0x02, 0x00, 0x00, 0x02 };
static const uint8_t commit_start[] = { 0x00, 0x00, 0x00, 0x07, 0x50, 0x16,
					0x05, 0x00, 0x00, 0x00, 0x20 };
/* Header of the store command: 7 byte APDU head + 32 byte id = 0x27. */
static const uint8_t commit_prefix[] = { 0x00, 0x00, 0x00, 0x27, 0x50, 0x16,
					 0x03, 0x00, 0x00, 0x00, 0x20 };

const struct egis_cmd egis_cmd_arm_enroll = { arm_enroll, sizeof(arm_enroll) };
const struct egis_cmd egis_cmd_enroll_start = { enroll_start,
						sizeof(enroll_start) };
const struct egis_cmd egis_cmd_start_capture = { start_capture,
						 sizeof(start_capture) };
const struct egis_cmd egis_cmd_read_capture = { read_capture,
						sizeof(read_capture) };
const struct egis_cmd egis_cmd_commit_start = { commit_start,
						sizeof(commit_start) };
#endif

/* "check" command header: CLA INS P1 P2 P3 for both prefix variants. */
static const uint8_t check_prefix_type1[] = { 0x50, 0x17, 0x03, 0x00, 0x00 };
static const uint8_t check_prefix_type2[] = { 0x50, 0x17, 0x03, 0x80, 0x00 };
static const uint8_t check_suffix[] = { 0x00, 0x40 };
#define CHECK_SEPARATOR_LEN 32u

static uint32_t sum_be16(const uint8_t *buf, size_t len) {
	uint32_t sum = 0;
	size_t i;

	for (i = 0; i + 1 < len; i += 2)
		sum += fpal_get_be16(buf + i);
	if (i < len) /* odd length: implicit zero pad byte */
		sum += (uint32_t)buf[i] << 8;
	return sum;
}

uint16_t egis_checksum(const uint8_t *frame, size_t len) {
	return (uint16_t)(0xffffu - (sum_be16(frame, len) % 0xffffu));
}

int egis_frame(const uint8_t *payload, size_t plen, uint8_t *out, size_t cap,
	       size_t *out_len) {
	const size_t len = EGIS_HDR_LEN + plen;

	if (!payload || !out || !out_len)
		return FPAL_E_INVAL;
	if (len > cap)
		return FPAL_E_OVERFLOW;

	memcpy(out, write_magic, EGIS_MAGIC_LEN);
	fpal_put_be16(out + EGIS_MAGIC_LEN, 0);
	memcpy(out + EGIS_HDR_LEN, payload, plen);
	fpal_put_be16(out + EGIS_MAGIC_LEN, egis_checksum(out, len));

	*out_len = len;
	return FPAL_OK;
}

int egis_parse(const uint8_t *buf, size_t len, struct egis_rsp *rsp) {
	uint32_t plen;
	size_t frame_len;

	if (!buf || !rsp)
		return FPAL_E_INVAL;
	if (len < EGIS_RSP_BODY_OFF + EGIS_SW_LEN) {
		fpal_dbg("response too short (%zu bytes)", len);
		return FPAL_E_PROTO;
	}
	if (memcmp(buf, read_magic, EGIS_MAGIC_LEN) != 0) {
		fpal_dbg("response has bad magic");
		return FPAL_E_PROTO;
	}

	/* Length field covers the body plus the status word. */
	plen = fpal_get_be32(buf + EGIS_HDR_LEN);
	if (plen < EGIS_SW_LEN || plen > len - EGIS_RSP_BODY_OFF) {
		fpal_dbg("response length field %u does not fit %zu bytes",
			plen, len);
		return FPAL_E_PROTO;
	}

	frame_len = EGIS_RSP_BODY_OFF + plen;
	if (frame_len != len)
		fpal_dbg("response has %zu trailing bytes", len - frame_len);

	rsp->body = buf + EGIS_RSP_BODY_OFF;
	rsp->body_len = plen - EGIS_SW_LEN;
	rsp->sw = fpal_get_be16(rsp->body + rsp->body_len);
	rsp->checksum_ok = sum_be16(buf, frame_len) % 0xffffu == 0;
	return FPAL_OK;
}

int egis_build_check(const struct fpal_template_id *ids, size_t n,
		     uint32_t flags, uint8_t *out, size_t cap, size_t *out_len) {
	const uint8_t *prefix = (flags & EGIS_FLAG_CHECK_PREFIX_TYPE2) ?
					check_prefix_type2 :
					check_prefix_type1;
	/* The chip expects one zero "separator" id in front of the real ones. */
	const size_t ids_len = (n + 1) * EGIS_ID_LEN;
	const size_t body_len = sizeof(check_prefix_type1) + 2 + ids_len +
				sizeof(check_suffix);
	const size_t len = 4 + body_len;
	uint8_t *p = out;

	if (!out || !out_len || (n && !ids))
		return FPAL_E_INVAL;
	if (n > EGIS_MAX_TEMPLATES)
		return FPAL_E_INVAL;
	if (len > cap)
		return FPAL_E_OVERFLOW;

	fpal_put_be32(p, (uint32_t)body_len);
	p += 4;
	memcpy(p, prefix, sizeof(check_prefix_type1));
	p += sizeof(check_prefix_type1);
	fpal_put_be16(p, (uint16_t)ids_len);
	p += 2;
	memset(p, 0, CHECK_SEPARATOR_LEN);
	p += CHECK_SEPARATOR_LEN;
	for (size_t i = 0; i < n; i++) {
		memcpy(p, ids[i].bytes, EGIS_ID_LEN);
		p += EGIS_ID_LEN;
	}
	memcpy(p, check_suffix, sizeof(check_suffix));

	*out_len = len;
	return FPAL_OK;
}

#ifdef FPAL_ENABLE_ENROLL
/*
 * Status words seen in the enrol loop (libfprint egismoc): an accepted scan
 * answers 90 00 with a 2 byte body, an off-centre finger 64 91 with a 2 byte
 * body, a dirty sensor 64 xx with no body.
 */
enum egis_capture egis_classify_capture(const struct egis_rsp *rsp) {
	if (rsp->body_len == 2 && rsp->sw == EGIS_SW_OK)
		return EGIS_CAPTURE_OK;
	if (rsp->body_len == 2 && rsp->sw == 0x6491u)
		return EGIS_CAPTURE_CENTER;
	if (rsp->body_len == 0 && (rsp->sw >> 8) == 0x64u)
		return EGIS_CAPTURE_DIRTY;
	return EGIS_CAPTURE_RETRY;
}

int egis_build_commit(const struct fpal_template_id *id, uint8_t *out,
		      size_t cap, size_t *out_len) {
	const size_t len = sizeof(commit_prefix) + EGIS_ID_LEN;

	if (!id || !out || !out_len)
		return FPAL_E_INVAL;
	if (len > cap)
		return FPAL_E_OVERFLOW;
	memcpy(out, commit_prefix, sizeof(commit_prefix));
	memcpy(out + sizeof(commit_prefix), id->bytes, EGIS_ID_LEN);
	*out_len = len;
	return FPAL_OK;
}
#endif

#ifdef FPAL_ENABLE_ENROLL
/*
 * 00 00 | BE16(7 + 32) | 50 18 04 00 00 | BE16(32) | id, as the Windows
 * driver and libfprint send it for a single template.
 */
static const uint8_t delete_prefix[] = { 0x50, 0x18, 0x04, 0x00, 0x00 };

int egis_build_delete(const struct fpal_template_id *id, uint8_t *out,
		      size_t cap, size_t *out_len) {
	const size_t body = sizeof(delete_prefix) + 2 + EGIS_ID_LEN;
	const size_t len = 4 + body;
	uint8_t *p = out;

	if (!id || !out || !out_len)
		return FPAL_E_INVAL;
	if (len > cap)
		return FPAL_E_OVERFLOW;
	fpal_put_be32(p, (uint32_t)body);
	p += 4;
	memcpy(p, delete_prefix, sizeof(delete_prefix));
	p += sizeof(delete_prefix);
	fpal_put_be16(p, EGIS_ID_LEN);
	p += 2;
	memcpy(p, id->bytes, EGIS_ID_LEN);
	*out_len = len;
	return FPAL_OK;
}
#endif

int egis_parse_list(const struct egis_rsp *rsp, struct fpal_template_id *out,
		    size_t cap, size_t *n) {
	size_t count;

	if (!rsp || !n || (cap && !out))
		return FPAL_E_INVAL;
	if (rsp->sw != EGIS_SW_OK)
		return FPAL_E_PROTO;
	/* Not seen in practice; keep the whole ids and report the rest. */
	if (rsp->body_len % EGIS_ID_LEN != 0)
		fpal_warn("list body length %zu is not a multiple of %u",
			 rsp->body_len, EGIS_ID_LEN);

	count = rsp->body_len / EGIS_ID_LEN;
	if (count > cap)
		return FPAL_E_OVERFLOW;

	for (size_t i = 0; i < count; i++)
		memcpy(out[i].bytes, rsp->body + i * EGIS_ID_LEN,
		       EGIS_ID_LEN);
	*n = count;
	return FPAL_OK;
}
