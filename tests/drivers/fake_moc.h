/* SPDX-License-Identifier: Apache-2.0 */
/*
 * A made-up Match-on-Chip sensor, only for the tests. It is deliberately NOT
 * like the Egis one: 20 byte ids, room for 3 templates, commands are one
 * byte, the finger is announced by a bulk IN event, and it needs a vendor
 * control-out at open time. If the driver API is general, this driver can be
 * written against it and pass the same conformance tests as the real ones.
 * It is also the worked example in docs/adding-a-sensor.md.
 *
 * The file holds both halves: the driver (fpal_driver_fake_moc) and a
 * simulated chip behind a transport (fake_chip) that really stores
 * templates, so enrol and delete change what the next list returns.
 */
#ifndef FPAL_TESTS_FAKE_MOC_H
#define FPAL_TESTS_FAKE_MOC_H

#include "core/driver.h"

#define FAKE_VID 0xfa4eu
#define FAKE_PID 0x0001u

#define FAKE_ID_LEN 20u
#define FAKE_MAX_TEMPLATES 3u

#define FAKE_IFACE 0
#define FAKE_EP_OUT 0x01
#define FAKE_EP_IN 0x81
#define FAKE_EP_EVENT 0x83

/* Bulk commands: first byte of every frame the host sends. */
#define FAKE_CMD_RESET 0x00
#define FAKE_CMD_LIST 0x10
#define FAKE_CMD_ARM_IDENTIFY 0x20
#define FAKE_CMD_ENROLL_BEGIN 0x30
#define FAKE_CMD_STORE 0x31 /* + id */
#define FAKE_CMD_DELETE 0x40 /* + id */

/* Events that arrive on FAKE_EP_EVENT. */
#define FAKE_EVT_MATCH 0x01 /* + id */
#define FAKE_EVT_NOMATCH 0x02
#define FAKE_EVT_STAGE 0x03
#define FAKE_EVT_DUPLICATE 0x04
#define FAKE_ENROLL_STAGES 3

extern const struct fpal_driver fpal_driver_fake_moc;
extern const struct fpal_usb_id fake_moc_usb_id;

#define FAKE_LOG_MAX 64u
#define FAKE_EVENTS_MAX 16u

struct fake_chip {
	/* What the chip stores. */
	uint8_t ids[FAKE_MAX_TEMPLATES][FAKE_ID_LEN];
	size_t n;

	/* Events waiting for the host, served in order; an empty queue means
	 * "no finger": the wait times out. */
	uint8_t events[FAKE_EVENTS_MAX][1 + FAKE_ID_LEN];
	size_t event_len[FAKE_EVENTS_MAX];
	size_t n_events, i_events;
	bool cancelled;

	/* Misbehaviour switches for the safety tests. */
	bool lie_store;	 /* acknowledge a store but keep nothing */
	bool lie_delete; /* acknowledge a delete but keep everything */
	bool delete_two; /* a delete also removes the first other template */
	bool garbage;	 /* answer every command with junk */

	/* What the host did. */
	uint8_t cmds[FAKE_LOG_MAX]; /* first byte of each bulk OUT frame */
	size_t n_cmds;
	unsigned claims, releases, resets, ctrl_outs, waits;

	/* Pending answer to the last command. */
	uint8_t rsp[2 + FAKE_MAX_TEMPLATES * FAKE_ID_LEN];
	size_t rsp_len;
	bool rsp_ready;
};

void fake_chip_init(struct fake_chip *c);
struct fpal_transport fake_chip_transport(struct fake_chip *c);

/* Put a template on the chip (id of FAKE_ID_LEN bytes, filled with @seed). */
void fake_chip_add(struct fake_chip *c, uint8_t seed);
void fake_chip_id(uint8_t seed, struct fpal_template_id *out);

/* Script what the finger does next. */
void fake_chip_touch_stored(struct fake_chip *c, size_t index);
void fake_chip_touch_unknown(struct fake_chip *c);
void fake_chip_script_enroll(struct fake_chip *c, bool duplicate_finger);

/* Open the driver on the chip. */
int fake_chip_open(struct fake_chip *c, struct fpal_device **dev);

#endif /* FPAL_TESTS_FAKE_MOC_H */
