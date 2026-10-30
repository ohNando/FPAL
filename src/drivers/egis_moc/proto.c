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

/* "check" command header: CLA INS P1 P2 P3 for both prefix variants. */
static const uint8_t check_prefix_type1[] = { 0x50, 0x17, 0x03, 0x00, 0x00 };
static const uint8_t check_prefix_type2[] = { 0x50, 0x17, 0x03, 0x80, 0x00 };
static const uint8_t check_suffix[] = { 0x00, 0x40 };
#define CHECK_SEPARATOR_LEN 32u

static uint32_t sum_be16(const uint8_t *buf, size_t len) {
	uint32_t sum = 0;
	size_t i;

	for (i = 0; i + 1 < len; i += 2)
		sum += fpx_get_be16(buf + i);
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
		return FPX_E_INVAL;
	if (len > cap)
		return FPX_E_OVERFLOW;

	memcpy(out, write_magic, EGIS_MAGIC_LEN);
	fpx_put_be16(out + EGIS_MAGIC_LEN, 0);
	memcpy(out + EGIS_HDR_LEN, payload, plen);
	fpx_put_be16(out + EGIS_MAGIC_LEN, egis_checksum(out, len));

	*out_len = len;
	return FPX_OK;
}

int egis_parse(const uint8_t *buf, size_t len, struct egis_rsp *rsp) {
	uint32_t plen;
	size_t frame_len;

	if (!buf || !rsp)
		return FPX_E_INVAL;
	if (len < EGIS_RSP_BODY_OFF + EGIS_SW_LEN) {
		fpx_dbg("response too short (%zu bytes)", len);
		return FPX_E_PROTO;
	}
	if (memcmp(buf, read_magic, EGIS_MAGIC_LEN) != 0) {
		fpx_dbg("response has bad magic");
		return FPX_E_PROTO;
	}

	/* Length field covers the body plus the status word. */
	plen = fpx_get_be32(buf + EGIS_HDR_LEN);
	if (plen < EGIS_SW_LEN || plen > len - EGIS_RSP_BODY_OFF) {
		fpx_dbg("response length field %u does not fit %zu bytes",
			plen, len);
		return FPX_E_PROTO;
	}

	frame_len = EGIS_RSP_BODY_OFF + plen;
	if (frame_len != len)
		fpx_dbg("response has %zu trailing bytes", len - frame_len);

	rsp->body = buf + EGIS_RSP_BODY_OFF;
	rsp->body_len = plen - EGIS_SW_LEN;
	rsp->sw = fpx_get_be16(rsp->body + rsp->body_len);
	rsp->checksum_ok = sum_be16(buf, frame_len) % 0xffffu == 0;
	return FPX_OK;
}

int egis_build_check(const struct fpx_template_id *ids, size_t n,
		     uint32_t flags, uint8_t *out, size_t cap, size_t *out_len) {
	const uint8_t *prefix = (flags & EGIS_FLAG_CHECK_PREFIX_TYPE2) ?
					check_prefix_type2 :
					check_prefix_type1;
	/* The chip expects one zero "separator" id in front of the real ones. */
	const size_t ids_len = (n + 1) * FPX_TEMPLATE_ID_LEN;
	const size_t body_len = sizeof(check_prefix_type1) + 2 + ids_len +
				sizeof(check_suffix);
	const size_t len = 4 + body_len;
	uint8_t *p = out;

	if (!out || !out_len || (n && !ids))
		return FPX_E_INVAL;
	if (n > FPX_MAX_TEMPLATES)
		return FPX_E_INVAL;
	if (len > cap)
		return FPX_E_OVERFLOW;

	fpx_put_be32(p, (uint32_t)body_len);
	p += 4;
	memcpy(p, prefix, sizeof(check_prefix_type1));
	p += sizeof(check_prefix_type1);
	fpx_put_be16(p, (uint16_t)ids_len);
	p += 2;
	memset(p, 0, CHECK_SEPARATOR_LEN);
	p += CHECK_SEPARATOR_LEN;
	for (size_t i = 0; i < n; i++) {
		memcpy(p, ids[i].bytes, FPX_TEMPLATE_ID_LEN);
		p += FPX_TEMPLATE_ID_LEN;
	}
	memcpy(p, check_suffix, sizeof(check_suffix));

	*out_len = len;
	return FPX_OK;
}

int egis_parse_list(const struct egis_rsp *rsp, struct fpx_template_id *out,
		    size_t cap, size_t *n) {
	size_t count;

	if (!rsp || !n || (cap && !out))
		return FPX_E_INVAL;
	if (rsp->sw != EGIS_SW_OK)
		return FPX_E_PROTO;
	/* Not seen in practice; keep the whole ids and report the rest. */
	if (rsp->body_len % FPX_TEMPLATE_ID_LEN != 0)
		fpx_warn("list body length %zu is not a multiple of %u",
			 rsp->body_len, FPX_TEMPLATE_ID_LEN);

	count = rsp->body_len / FPX_TEMPLATE_ID_LEN;
	if (count > cap)
		return FPX_E_OVERFLOW;

	for (size_t i = 0; i < count; i++)
		memcpy(out[i].bytes, rsp->body + i * FPX_TEMPLATE_ID_LEN,
		       FPX_TEMPLATE_ID_LEN);
	*n = count;
	return FPX_OK;
}
