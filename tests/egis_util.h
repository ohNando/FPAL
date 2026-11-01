/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Helpers shared by the Egis driver tests: open a device on the scripted
 * mock transport and inspect or queue frames.
 */
#ifndef FPAL_TESTS_EGIS_UTIL_H
#define FPAL_TESTS_EGIS_UTIL_H

#include "core/driver.h"
#include "drivers/egis_moc/egis_moc_proto.h"
#include "mock_transport.h"
#include "t.h"

#define FW "1.0.0.4867"

/* Payload of the n-th recorded OUT frame (after the EGIS header). */
static inline const uint8_t *out_payload(const struct mock *m, size_t i) {
	return m->out[i].data + EGIS_HDR_LEN;
}

static inline bool out_is(const struct mock *m, size_t i, const struct egis_cmd *c) {
	return i < m->n_out && m->out[i].len == EGIS_HDR_LEN + c->len &&
	       memcmp(out_payload(m, i), c->data, c->len) == 0;
}

static inline void make_ids(struct fpal_template_id *ids, size_t n) {
	for (size_t i = 0; i < n; i++)
		memset(ids[i].bytes, (int)('A' + i), FPAL_TEMPLATE_ID_LEN);
}

static inline void push_list(struct mock *m, const struct fpal_template_id *ids,
		      size_t n) {
	uint8_t body[FPAL_MAX_TEMPLATES * FPAL_TEMPLATE_ID_LEN];

	for (size_t i = 0; i < n; i++)
		memcpy(body + i * FPAL_TEMPLATE_ID_LEN, ids[i].bytes,
		       FPAL_TEMPLATE_ID_LEN);
	mock_push_egis(m, body, n * FPAL_TEMPLATE_ID_LEN, EGIS_SW_OK);
}

static inline void push_ok(struct mock *m) {
	mock_push_egis(m, NULL, 0, EGIS_SW_OK);
}

/* Open a 1c7a:@pid device on @m; the fw version response is queued here. */
static inline struct fpal_device *open_dev_pid(struct mock *m, uint16_t pid) {
	const struct fpal_usb_id *id = NULL;
	const struct fpal_driver *drv = fpal_driver_lookup(0x1c7a, pid, &id);
	struct fpal_device *dev = NULL;

	mock_push_egis(m, (const uint8_t *)FW, sizeof(FW) - 1, EGIS_SW_OK);
	if (!drv ||
	    fpal_device_open_transport(drv, id, mock_transport(m), -1, &dev))
		return NULL;
	return dev;
}

static inline struct fpal_device *open_dev(struct mock *m) {
	return open_dev_pid(m, 0x0587);
}

#endif /* FPAL_TESTS_EGIS_UTIL_H */
