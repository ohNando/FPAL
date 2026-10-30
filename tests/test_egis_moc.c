// SPDX-License-Identifier: Apache-2.0
/*
 * Egis MoC driver state machine tests against the scripted mock transport.
 */
#include "core/driver.h"
#include "drivers/egis_moc/egis_moc_proto.h"
#include "mock_transport.h"
#include "t.h"

#define FW "1.0.0.4867"

/* Payload of the n-th recorded OUT frame (after the EGIS header). */
static const uint8_t *out_payload(const struct mock *m, size_t i) {
	return m->out[i].data + EGIS_HDR_LEN;
}

static bool out_is(const struct mock *m, size_t i, const struct egis_cmd *c) {
	return i < m->n_out && m->out[i].len == EGIS_HDR_LEN + c->len &&
	       memcmp(out_payload(m, i), c->data, c->len) == 0;
}

static void make_ids(struct fpx_template_id *ids, size_t n) {
	for (size_t i = 0; i < n; i++)
		memset(ids[i].bytes, (int)('A' + i), FPX_TEMPLATE_ID_LEN);
}

static void push_list(struct mock *m, const struct fpx_template_id *ids,
		      size_t n) {
	uint8_t body[FPX_MAX_TEMPLATES * FPX_TEMPLATE_ID_LEN];

	for (size_t i = 0; i < n; i++)
		memcpy(body + i * FPX_TEMPLATE_ID_LEN, ids[i].bytes,
		       FPX_TEMPLATE_ID_LEN);
	mock_push_egis(m, body, n * FPX_TEMPLATE_ID_LEN, EGIS_SW_OK);
}

static void push_ok(struct mock *m) {
	mock_push_egis(m, NULL, 0, EGIS_SW_OK);
}

/* Open a 1c7a:0587 device on @m; the fw version response is queued here. */
static struct fpx_device *open_dev(struct mock *m) {
	const struct fpx_usb_id *id = NULL;
	const struct fpx_driver *drv = fpx_driver_lookup(0x1c7a, 0x0587, &id);
	struct fpx_device *dev = NULL;

	mock_push_egis(m, (const uint8_t *)FW, sizeof(FW) - 1, EGIS_SW_OK);
	if (!drv ||
	    fpx_device_open_transport(drv, id, mock_transport(m), -1, &dev))
		return NULL;
	return dev;
}

static void test_open_reads_fw_version(void) {
	struct mock m;
	struct fpx_device *dev;

	mock_init(&m);
	dev = open_dev(&m);
	T_CHECK(dev != NULL);
	if (!dev)
		return;

	T_EQ_INT(m.resets, 1);
	T_EQ_INT(m.ctrls, 5);
	T_CHECK(out_is(&m, 0, &egis_cmd_fw_version));
	T_CHECK(strcmp(fpx_device_fw_version(dev), FW) == 0);
	T_CHECK(strcmp(fpx_device_driver_name(dev), "egis_moc") == 0);

	fpx_device_close(dev);
	T_EQ_INT(m.releases, m.claims);
}

static void test_open_busy_does_not_reset(void) {
	const struct fpx_usb_id *id = NULL;
	const struct fpx_driver *drv = fpx_driver_lookup(0x1c7a, 0x0587, &id);
	struct fpx_device *dev = NULL;
	struct mock m;

	mock_init(&m);
	m.claim_rc = FPX_E_BUSY;
	T_EQ_INT(fpx_device_open_transport(drv, id, mock_transport(&m), -1,
					   &dev),
		 FPX_E_BUSY);
	T_EQ_INT(m.resets, 0);
	T_EQ_INT(m.n_out, 0);
}

static void test_list(void) {
	struct fpx_template_id ids[3], got[FPX_MAX_TEMPLATES];
	struct fpx_device *dev;
	struct mock m;
	size_t n = 0;

	mock_init(&m);
	make_ids(ids, 3);
	dev = open_dev(&m);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	push_list(&m, ids, 3);

	T_EQ_INT(fpx_list(dev, got, FPX_MAX_TEMPLATES, &n), FPX_OK);
	T_EQ_INT(n, 3);
	T_EQ_MEM(got, ids, sizeof(ids));
	T_CHECK(out_is(&m, 1, &egis_cmd_list));
	fpx_device_close(dev);
}

/* Queue list + reset + arm + check responses; the match answer is next. */
static struct fpx_device *identify_setup(struct mock *m,
					 struct fpx_template_id *ids, size_t n) {
	struct fpx_device *dev;

	mock_init(m);
	make_ids(ids, n);
	dev = open_dev(m);
	if (!dev)
		return NULL;
	push_list(m, ids, n);
	push_ok(m); /* reset */
	push_ok(m); /* arm identify */
	push_ok(m); /* sensor check */
	return dev;
}

static void test_identify_match(void) {
	struct fpx_template_id ids[2], match;
	uint8_t body[EGIS_IDENTIFY_ID_BODY_OFF + FPX_TEMPLATE_ID_LEN];
	struct fpx_device *dev;
	struct mock m;

	dev = identify_setup(&m, ids, 2);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	memset(body, 0xee, EGIS_IDENTIFY_ID_BODY_OFF);
	memcpy(body + EGIS_IDENTIFY_ID_BODY_OFF, ids[1].bytes,
	       FPX_TEMPLATE_ID_LEN);
	mock_push_egis(&m, body, sizeof(body), EGIS_SW_OK);
	push_ok(&m); /* final reset */

	T_EQ_INT(fpx_identify(dev, 1000, &match), FPX_OK);
	T_EQ_MEM(match.bytes, ids[1].bytes, FPX_TEMPLATE_ID_LEN);
	T_EQ_INT(m.irqs, 1);

	/* fw, list, reset, arm, check, match, reset */
	T_EQ_INT(m.n_out, 7);
	T_CHECK(out_is(&m, 2, &egis_cmd_sensor_reset));
	T_CHECK(out_is(&m, 3, &egis_cmd_arm_identify));
	T_CHECK(out_is(&m, 4, &egis_cmd_sensor_check));
	/* the match request carries both stored ids */
	T_EQ_MEM(out_payload(&m, 5) + 43, ids[0].bytes, FPX_TEMPLATE_ID_LEN);
	T_EQ_MEM(out_payload(&m, 5) + 75, ids[1].bytes, FPX_TEMPLATE_ID_LEN);
	T_CHECK(out_is(&m, 6, &egis_cmd_sensor_reset));
	fpx_device_close(dev);
}

static void test_identify_no_match(void) {
	struct fpx_template_id ids[1], match;
	struct fpx_device *dev;
	struct mock m;

	dev = identify_setup(&m, ids, 1);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	mock_push_egis(&m, NULL, 0, EGIS_SW_NOMATCH);
	push_ok(&m);

	T_EQ_INT(fpx_identify(dev, 1000, &match), FPX_E_NOMATCH);
	T_CHECK(out_is(&m, m.n_out - 1, &egis_cmd_sensor_reset));
	fpx_device_close(dev);
}

/* A matched id that is not on the sensor means a wrong offset: refuse. */
static void test_identify_unknown_id_is_proto_error(void) {
	struct fpx_template_id ids[1], match;
	uint8_t body[EGIS_IDENTIFY_ID_BODY_OFF + FPX_TEMPLATE_ID_LEN];
	struct fpx_device *dev;
	struct mock m;

	dev = identify_setup(&m, ids, 1);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	memset(body, 0x5a, sizeof(body));
	mock_push_egis(&m, body, sizeof(body), EGIS_SW_OK);
	push_ok(&m);

	T_EQ_INT(fpx_identify(dev, 1000, &match), FPX_E_PROTO);
	fpx_device_close(dev);
}

static void test_identify_timeout_still_resets(void) {
	struct fpx_template_id ids[1], match;
	struct fpx_device *dev;
	struct mock m;

	dev = identify_setup(&m, ids, 1);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	m.irq_rc = FPX_E_TIMEOUT;
	/* the queued "sensor check" ok is consumed by the final reset */

	T_EQ_INT(fpx_identify(dev, 1000, &match), FPX_E_TIMEOUT);
	/* fw, list, reset, arm, reset */
	T_EQ_INT(m.n_out, 5);
	T_CHECK(out_is(&m, 4, &egis_cmd_sensor_reset));
	fpx_device_close(dev);
}

static void test_identify_cancel(void) {
	struct fpx_template_id ids[1], match;
	struct fpx_device *dev;
	struct mock m;

	dev = identify_setup(&m, ids, 1);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	fpx_cancel(dev);

	T_EQ_INT(fpx_identify(dev, 1000, &match), FPX_E_CANCELLED);
	T_CHECK(out_is(&m, m.n_out - 1, &egis_cmd_sensor_reset));
	fpx_device_close(dev);
}

static void test_identify_empty_sensor(void) {
	struct fpx_template_id match;
	struct fpx_device *dev;
	struct mock m;

	mock_init(&m);
	dev = open_dev(&m);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	push_list(&m, NULL, 0);

	T_EQ_INT(fpx_identify(dev, 1000, &match), FPX_E_NODATA);
	T_EQ_INT(m.irqs, 0);
	fpx_device_close(dev);
}

/* A corrupted response must not be trusted. */
static void test_bad_checksum_rejected(void) {
	struct fpx_template_id ids[1], got[FPX_MAX_TEMPLATES];
	struct fpx_device *dev;
	struct mock m;
	size_t n = 0;

	mock_init(&m);
	make_ids(ids, 1);
	dev = open_dev(&m);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	push_list(&m, ids, 1);
	m.in[m.n_in - 1].data[EGIS_RSP_BODY_OFF] ^= 0x01;

	T_EQ_INT(fpx_list(dev, got, FPX_MAX_TEMPLATES, &n), FPX_E_PROTO);
	fpx_device_close(dev);
}

int main(void) {
	T_RUN(test_open_reads_fw_version);
	T_RUN(test_open_busy_does_not_reset);
	T_RUN(test_list);
	T_RUN(test_identify_match);
	T_RUN(test_identify_no_match);
	T_RUN(test_identify_unknown_id_is_proto_error);
	T_RUN(test_identify_timeout_still_resets);
	T_RUN(test_identify_cancel);
	T_RUN(test_identify_empty_sensor);
	T_RUN(test_bad_checksum_rejected);
	return T_DONE();
}
