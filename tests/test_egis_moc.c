// SPDX-License-Identifier: Apache-2.0
/*
 * Egis MoC driver state machine tests against the scripted mock transport.
 */
#include "egis_util.h"

static void test_open_reads_fw_version(void) {
	struct mock m;
	struct fpal_device *dev;

	mock_init(&m);
	dev = open_dev(&m);
	T_CHECK(dev != NULL);
	if (!dev)
		return;

	T_EQ_INT(m.resets, 1);
	T_EQ_INT(m.ctrls, 5);
	T_CHECK(out_is(&m, 0, &egis_cmd_fw_version));
	T_CHECK(strcmp(fpal_device_fw_version(dev), FW) == 0);
	T_CHECK(strcmp(fpal_device_driver_name(dev), "egis_moc") == 0);

	fpal_device_close(dev);
	T_EQ_INT(m.releases, m.claims);
}

static void test_open_busy_does_not_reset(void) {
	const struct fpal_usb_id *id = NULL;
	const struct fpal_driver *drv = fpal_driver_lookup(0x1c7a, 0x0587, &id);
	struct fpal_device *dev = NULL;
	struct mock m;

	mock_init(&m);
	m.claim_rc = FPAL_E_BUSY;
	T_EQ_INT(fpal_device_open_transport(drv, id, mock_transport(&m), -1,
					   &dev),
		 FPAL_E_BUSY);
	T_EQ_INT(m.resets, 0);
	T_EQ_INT(m.n_out, 0);
}

static void test_list(void) {
	struct fpal_template_id ids[3], got[FPAL_MAX_TEMPLATES];
	struct fpal_device *dev;
	struct mock m;
	size_t n = 0;

	mock_init(&m);
	make_ids(ids, 3);
	dev = open_dev(&m);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	push_list(&m, ids, 3);

	T_EQ_INT(fpal_list(dev, got, FPAL_MAX_TEMPLATES, &n), FPAL_OK);
	T_EQ_INT(n, 3);
	T_EQ_MEM(got, ids, sizeof(ids));
	T_CHECK(out_is(&m, 1, &egis_cmd_list));
	fpal_device_close(dev);
}

/* Queue list + reset + arm + check responses; the match answer is next. */
static struct fpal_device *identify_setup(struct mock *m,
					 struct fpal_template_id *ids, size_t n) {
	struct fpal_device *dev;

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
	struct fpal_template_id ids[2], match;
	uint8_t body[EGIS_IDENTIFY_ID_BODY_OFF + FPAL_TEMPLATE_ID_LEN];
	struct fpal_device *dev;
	struct mock m;

	dev = identify_setup(&m, ids, 2);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	memset(body, 0xee, EGIS_IDENTIFY_ID_BODY_OFF);
	memcpy(body + EGIS_IDENTIFY_ID_BODY_OFF, ids[1].bytes,
	       FPAL_TEMPLATE_ID_LEN);
	mock_push_egis(&m, body, sizeof(body), EGIS_SW_OK);
	push_ok(&m); /* final reset */

	T_EQ_INT(fpal_identify(dev, 1000, &match), FPAL_OK);
	T_EQ_MEM(match.bytes, ids[1].bytes, FPAL_TEMPLATE_ID_LEN);
	T_EQ_INT(m.irqs, 1);

	/* fw, list, reset, arm, check, match, reset */
	T_EQ_INT(m.n_out, 7);
	T_CHECK(out_is(&m, 2, &egis_cmd_sensor_reset));
	T_CHECK(out_is(&m, 3, &egis_cmd_arm_identify));
	T_CHECK(out_is(&m, 4, &egis_cmd_sensor_check));
	/* the match request carries both stored ids */
	T_EQ_MEM(out_payload(&m, 5) + 43, ids[0].bytes, FPAL_TEMPLATE_ID_LEN);
	T_EQ_MEM(out_payload(&m, 5) + 75, ids[1].bytes, FPAL_TEMPLATE_ID_LEN);
	T_CHECK(out_is(&m, 6, &egis_cmd_sensor_reset));
	fpal_device_close(dev);
}

static void test_identify_no_match(void) {
	struct fpal_template_id ids[1], match;
	struct fpal_device *dev;
	struct mock m;

	dev = identify_setup(&m, ids, 1);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	mock_push_egis(&m, NULL, 0, EGIS_SW_NOMATCH);
	push_ok(&m);

	T_EQ_INT(fpal_identify(dev, 1000, &match), FPAL_E_NOMATCH);
	T_CHECK(out_is(&m, m.n_out - 1, &egis_cmd_sensor_reset));
	fpal_device_close(dev);
}

/* A matched id that is not on the sensor means a wrong offset: refuse. */
static void test_identify_unknown_id_is_proto_error(void) {
	struct fpal_template_id ids[1], match;
	uint8_t body[EGIS_IDENTIFY_ID_BODY_OFF + FPAL_TEMPLATE_ID_LEN];
	struct fpal_device *dev;
	struct mock m;

	dev = identify_setup(&m, ids, 1);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	memset(body, 0x5a, sizeof(body));
	mock_push_egis(&m, body, sizeof(body), EGIS_SW_OK);
	push_ok(&m);

	T_EQ_INT(fpal_identify(dev, 1000, &match), FPAL_E_PROTO);
	fpal_device_close(dev);
}

static void test_identify_timeout_still_resets(void) {
	struct fpal_template_id ids[1], match;
	struct fpal_device *dev;
	struct mock m;

	dev = identify_setup(&m, ids, 1);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	m.irq_rc = FPAL_E_TIMEOUT;
	/* the queued "sensor check" ok is consumed by the final reset */

	T_EQ_INT(fpal_identify(dev, 1000, &match), FPAL_E_TIMEOUT);
	/* fw, list, reset, arm, reset */
	T_EQ_INT(m.n_out, 5);
	T_CHECK(out_is(&m, 4, &egis_cmd_sensor_reset));
	fpal_device_close(dev);
}

static void test_identify_cancel(void) {
	struct fpal_template_id ids[1], match;
	struct fpal_device *dev;
	struct mock m;

	dev = identify_setup(&m, ids, 1);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	fpal_cancel(dev);

	T_EQ_INT(fpal_identify(dev, 1000, &match), FPAL_E_CANCELLED);
	T_CHECK(out_is(&m, m.n_out - 1, &egis_cmd_sensor_reset));
	fpal_device_close(dev);
}

static void test_identify_empty_sensor(void) {
	struct fpal_template_id match;
	struct fpal_device *dev;
	struct mock m;

	mock_init(&m);
	dev = open_dev(&m);
	T_CHECK(dev != NULL);
	if (!dev)
		return;
	push_list(&m, NULL, 0);

	T_EQ_INT(fpal_identify(dev, 1000, &match), FPAL_E_NODATA);
	T_EQ_INT(m.irqs, 0);
	fpal_device_close(dev);
}

/* A corrupted response must not be trusted. */
static void test_bad_checksum_rejected(void) {
	struct fpal_template_id ids[1], got[FPAL_MAX_TEMPLATES];
	struct fpal_device *dev;
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

	T_EQ_INT(fpal_list(dev, got, FPAL_MAX_TEMPLATES, &n), FPAL_E_PROTO);
	fpal_device_close(dev);
}

#ifndef FPAL_ENABLE_ENROLL
/* A normal build must be unable to write to the sensor. */
static void test_default_build_cannot_enroll(void) {
	struct fpal_template_id id = { .bytes = "FPAL-test" };
	const struct fpal_driver *drv = fpal_driver_lookup(0x1c7a, 0x0587, NULL);
	struct fpal_device *dev;
	struct mock m;
	size_t sent;

	mock_init(&m);
	dev = open_dev(&m);
	T_CHECK(dev != NULL);
	T_CHECK(drv != NULL && drv->enroll == NULL);
	T_CHECK(drv->delete_template == NULL);
	T_CHECK(!(drv->caps & FPAL_CAP_ENROLL));

	sent = m.n_out;
	T_EQ_INT(fpal_enroll(dev, &id, 0, NULL, NULL), FPAL_E_NOTSUP);
	T_EQ_INT(fpal_enroll(dev, &id, FPAL_ENROLL_DRY_RUN, NULL, NULL),
		 FPAL_E_NOTSUP);
	T_EQ_INT(fpal_delete(dev, &id), FPAL_E_NOTSUP);
	T_EQ_INT(m.n_out, sent); /* nothing was sent */
	T_EQ_INT(fpal_template_id_generate(&id), FPAL_E_NOTSUP);
	fpal_device_close(dev);
}
#endif

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
#ifndef FPAL_ENABLE_ENROLL
	T_RUN(test_default_build_cannot_enroll);
#endif
	return T_DONE();
}
