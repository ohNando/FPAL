/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * EgisTec / LighTuning Match-on-Chip wire protocol.
 *
 * Protocol knowledge comes from the libfprint egismoc driver
 * (libfprint/drivers/egismoc), which was reverse engineered from USB
 * captures of the Windows driver.
 *
 * Command frame (host -> sensor, bulk OUT 0x02):
 *
 *   'E' 'G' 'I' 'S' 00 00 00 01 | CK CK | payload...
 *
 * Response frame (sensor -> host, bulk IN 0x81):
 *
 *   'S' 'I' 'G' 'E' 00 00 00 01 | ck ck | len(4, BE) | body... | SW1 SW2
 *
 * The payload starts with a 4-byte big-endian length followed by an APDU
 * like body: CLA(0x50) INS P1 P2 [00 Lh Ll]. SW1 SW2 = 90 00 means success.
 *
 * The delete opcode (INS 0x18) is intentionally not defined anywhere in
 * this project.
 */
#ifndef FPX_DRIVERS_EGIS_MOC_PROTO_H
#define FPX_DRIVERS_EGIS_MOC_PROTO_H

#include <stddef.h>
#include <stdint.h>

#include "fpx/types.h"

/* USB */
#define EGIS_IFACE 0u
#define EGIS_EP_OUT 0x02u
#define EGIS_EP_IN 0x81u
#define EGIS_EP_IRQ 0x83u

/* id table quirk flags */
#define EGIS_FLAG_CHECK_PREFIX_TYPE2 (1u << 0) /* 50 17 03 80 00 instead of 50 17 03 00 00 */
#define EGIS_FLAG_ENROLL_STAGES_20 (1u << 1)

/* Framing */
#define EGIS_MAGIC_LEN 8u
#define EGIS_CK_LEN 2u
#define EGIS_HDR_LEN (EGIS_MAGIC_LEN + EGIS_CK_LEN) /* 10 */
#define EGIS_RSP_LEN_FIELD 4u
#define EGIS_RSP_BODY_OFF (EGIS_HDR_LEN + EGIS_RSP_LEN_FIELD) /* 14 */
#define EGIS_SW_LEN 2u

/* Status words (SW1 << 8 | SW2) */
#define EGIS_SW_OK 0x9000u
#define EGIS_SW_NOMATCH 0x9004u

/* Identify response: matched id at body offset 32 (absolute offset 46). */
#define EGIS_IDENTIFY_ID_BODY_OFF 32u

/* A fixed command payload. */
struct egis_cmd {
	const uint8_t *data;
	uint8_t len;
};

extern const struct egis_cmd egis_cmd_fw_version;
extern const struct egis_cmd egis_cmd_list;
extern const struct egis_cmd egis_cmd_sensor_reset;
extern const struct egis_cmd egis_cmd_sensor_check;
extern const struct egis_cmd egis_cmd_arm_identify;

/* A parsed response; @body points into the caller's receive buffer. */
struct egis_rsp {
	const uint8_t *body; /* data between length field and status word */
	size_t body_len;
	uint16_t sw;
	bool checksum_ok; /* informational, the device's ck is not verified */
};

/*
 * Checksum: big-endian 16-bit sum over the frame (checksum field = 0, odd
 * length padded with one zero byte), then 0xFFFF - (sum % 0xFFFF).
 */
uint16_t egis_checksum(const uint8_t *frame, size_t len);

/* Wrap @payload into a full command frame in @out. */
int egis_frame(const uint8_t *payload, size_t plen, uint8_t *out, size_t cap,
	       size_t *out_len);

/* Validate and split a response frame. */
int egis_parse(const uint8_t *buf, size_t len, struct egis_rsp *rsp);

/*
 * Build the "check" payload sent after a finger touch: it lists every
 * template id on the sensor and the chip answers which one matched.
 */
int egis_build_check(const struct fpx_template_id *ids, size_t n,
		     uint32_t flags, uint8_t *out, size_t cap, size_t *out_len);

/* Extract template ids from a list response body. */
int egis_parse_list(const struct egis_rsp *rsp, struct fpx_template_id *out,
		    size_t cap, size_t *n);

#endif /* FPX_DRIVERS_EGIS_MOC_PROTO_H */
