// SPDX-License-Identifier: Apache-2.0
/*
 * Enrolling against the scripted mock transport. Not built with ENROLL=0.
 * The most important property: nothing but the final store command writes to
 * the sensor, and a dry run never sends it.
 */
#include "egis_util.h"

#define STAGES 20u

static const struct fpal_template_id new_id = {
	.bytes = "FPAL-0123456789abcdef0123456789"
};

struct events {
	unsigned check, touch, done, center, dirty, retry;
	uint8_t last_stage, total;
};

static void on_event(enum fpal_enroll_event ev, uint8_t stage, uint8_t total,
		     void *ud) {
	struct events *e = ud;

	e->last_stage = stage;
	e->total = total;
	switch (ev) {
	case FPAL_ENROLL_CHECK_TOUCH:
		e->check++;
		break;
	case FPAL_ENROLL_TOUCH:
		e->touch++;
		break;
	case FPAL_ENROLL_STAGE_DONE:
		e->done++;
		break;
	case FPAL_ENROLL_RETRY_CENTER:
		e->center++;
		break;
	case FPAL_ENROLL_RETRY_DIRTY:
		e->dirty++;
		break;
	case FPAL_ENROLL_RETRY:
		e->retry++;
		break;
	}
}

/* INS and P1 of the n-th recorded frame. */
static uint8_t out_ins(const struct mock *m, size_t i) {
	return out_payload(m, i)[5];
}

static uint8_t out_p1(const struct mock *m, size_t i) {
	return out_payload(m, i)[6];
}

/* Frames that would write to the sensor: INS 16 with P1 03 (store) or 05. */
static unsigned writes_sent(const struct mock *m) {
	unsigned n = 0;

	for (size_t i = 0; i < m->n_out; i++) {
		if (out_payload(m, i)[4] == 0x50 && out_ins(m, i) == 0x16 &&
		    (out_p1(m, i) == 0x03 || out_p1(m, i) == 0x05))
			n++;
	}
	return n;
}

static void push_status(struct mock *m, uint16_t sw) {
	mock_push_egis(m, NULL, 0, sw);
}

/* list, reset, arm, sensor check, duplicate check (@dup_sw), enrol start */
static void push_prefix(struct mock *m, const struct fpal_template_id *ids,
			size_t n, uint16_t dup_sw) {
	push_list(m, ids, n);
	push_ok(m);
	push_ok(m);
	push_ok(m);
	push_status(m, dup_sw);
	push_ok(m);
}

enum scan { SCAN_OK, SCAN_CENTER, SCAN_DIRTY, SCAN_ODD };

static void push_scan(struct mock *m, enum scan s) {
	static const uint8_t two[2] = { 0x00, 0x01 };

	push_ok(m); /* reset */
	push_ok(m); /* start capture */
	switch (s) {
	case SCAN_OK:
		mock_push_egis(m, two, sizeof(two), 0x9000);
		break;
	case SCAN_CENTER:
		mock_push_egis(m, two, sizeof(two), 0x6491);
		break;
	case SCAN_DIRTY:
		push_status(m, 0x6400);
		break;
	default:
		push_status(m, 0x1234);
		break;
	}
}

static void push_scans(struct mock *m, unsigned n) {
	for (unsigned i = 0; i < n; i++)
		push_scan(m, SCAN_OK);
}

static void test_full_enroll(void) {
	struct fpal_template_id before[2], after[3], store;
	struct fpal_device *dev;
	struct events ev = { 0 };
	struct mock m;
	size_t plen;
	uint8_t payload[64];

	make_ids(before, 2);
	memcpy(after, before, sizeof(before));
	after[2] = new_id;
	mock_init(&m);
	dev = open_dev(&m);
	push_prefix(&m, before, 2, EGIS_SW_NOMATCH);
	push_scans(&m, STAGES);
	push_ok(&m); /* commit start */
	push_ok(&m); /* store */
	push_ok(&m); /* final reset */
	push_list(&m, after, 3);

	m.n_out = 0;
	T_EQ_INT(fpal_enroll(dev, &new_id, 0, on_event, &ev), FPAL_OK);

	T_EQ_INT(m.n_out, 6 + 3 * STAGES + 4);
	T_CHECK(out_is(&m, 0, &egis_cmd_list));
	T_CHECK(out_is(&m, 1, &egis_cmd_sensor_reset));
	T_CHECK(out_is(&m, 2, &egis_cmd_arm_enroll));
	T_CHECK(out_is(&m, 3, &egis_cmd_sensor_check));
	T_EQ_INT(out_payload(&m, 4)[5], 0x17); /* the duplicate check */
	T_CHECK(out_is(&m, 5, &egis_cmd_enroll_start));
	T_CHECK(out_is(&m, 6, &egis_cmd_sensor_reset));
	T_CHECK(out_is(&m, 7, &egis_cmd_start_capture));
	T_CHECK(out_is(&m, 8, &egis_cmd_read_capture));

	/* exactly one store command, carrying our id, after all the scans */
	T_EQ_INT(writes_sent(&m), 2); /* commit start + store */
	T_CHECK(out_is(&m, 66, &egis_cmd_commit_start));
	T_EQ_INT(egis_build_commit(&new_id, payload, sizeof(payload), &plen), 0);
	T_EQ_INT(m.out[67].len, EGIS_HDR_LEN + plen);
	T_EQ_MEM(out_payload(&m, 67), payload, plen);
	T_CHECK(out_is(&m, 68, &egis_cmd_sensor_reset));
	T_CHECK(out_is(&m, 69, &egis_cmd_list));
	(void)store;

	T_EQ_INT(m.irqs, 1 + STAGES);
	T_EQ_INT(ev.done, STAGES);
	T_EQ_INT(ev.total, STAGES);
	T_EQ_INT(ev.check, 1);
	T_EQ_INT(ev.touch, STAGES);
	fpal_device_close(dev);
}

static void test_dry_run_never_writes(void) {
	struct fpal_template_id before[2];
	struct fpal_device *dev;
	struct events ev = { 0 };
	struct mock m;

	make_ids(before, 2);
	mock_init(&m);
	dev = open_dev(&m);
	push_prefix(&m, before, 2, EGIS_SW_NOMATCH);
	push_scans(&m, STAGES);
	push_ok(&m); /* final reset */

	m.n_out = 0;
	T_EQ_INT(fpal_enroll(dev, &new_id, FPAL_ENROLL_DRY_RUN, on_event, &ev),
		 FPAL_OK);
	T_EQ_INT(writes_sent(&m), 0);
	T_EQ_INT(m.n_out, 6 + 3 * STAGES + 1);
	T_CHECK(out_is(&m, m.n_out - 1, &egis_cmd_sensor_reset));
	T_EQ_INT(ev.done, STAGES);
	fpal_device_close(dev);
}

static void test_retries_do_not_count(void) {
	struct fpal_template_id before[1];
	struct fpal_device *dev;
	struct events ev = { 0 };
	struct mock m;

	make_ids(before, 1);
	mock_init(&m);
	dev = open_dev(&m);
	push_prefix(&m, before, 1, EGIS_SW_NOMATCH);
	push_scan(&m, SCAN_CENTER);
	push_scan(&m, SCAN_DIRTY);
	push_scan(&m, SCAN_ODD);
	push_scans(&m, STAGES);
	push_ok(&m);

	T_EQ_INT(fpal_enroll(dev, &new_id, FPAL_ENROLL_DRY_RUN, on_event, &ev),
		 FPAL_OK);
	T_EQ_INT(ev.center, 1);
	T_EQ_INT(ev.dirty, 1);
	T_EQ_INT(ev.retry, 1);
	T_EQ_INT(ev.done, STAGES);
	T_EQ_INT(ev.check, 1);
	T_EQ_INT(ev.touch, STAGES + 3);
	fpal_device_close(dev);
}

static void test_ten_stage_sensor(void) {
	struct fpal_device *dev;
	struct events ev = { 0 };
	struct mock m;

	mock_init(&m);
	dev = open_dev_pid(&m, 0x0582); /* no 20 stage flag */
	push_prefix(&m, NULL, 0, EGIS_SW_NOMATCH);
	push_scans(&m, 10);
	push_ok(&m);

	T_EQ_INT(fpal_enroll(dev, &new_id, FPAL_ENROLL_DRY_RUN, on_event, &ev),
		 FPAL_OK);
	T_EQ_INT(ev.total, 10);
	T_EQ_INT(ev.done, 10);
	fpal_device_close(dev);
}

static void test_duplicate_finger_stops_before_enrol_mode(void) {
	struct fpal_template_id before[2];
	struct fpal_device *dev;
	struct mock m;

	make_ids(before, 2);
	mock_init(&m);
	dev = open_dev(&m);
	push_list(&m, before, 2);
	push_ok(&m);
	push_ok(&m);
	push_ok(&m);
	push_status(&m, EGIS_SW_OK); /* the chip knows this finger */
	push_ok(&m);		     /* final reset */

	m.n_out = 0;
	T_EQ_INT(fpal_enroll(dev, &new_id, 0, NULL, NULL), FPAL_E_EXISTS);
	T_EQ_INT(writes_sent(&m), 0);
	T_EQ_INT(m.n_out, 6);
	for (size_t i = 0; i < m.n_out; i++)
		T_CHECK(!out_is(&m, i, &egis_cmd_enroll_start));
	T_CHECK(out_is(&m, m.n_out - 1, &egis_cmd_sensor_reset));
	fpal_device_close(dev);
}

static void test_full_sensor_sends_nothing_but_list(void) {
	struct fpal_template_id before[FPAL_MAX_TEMPLATES];
	struct fpal_device *dev;
	struct mock m;

	make_ids(before, FPAL_MAX_TEMPLATES);
	mock_init(&m);
	dev = open_dev(&m);
	push_list(&m, before, FPAL_MAX_TEMPLATES);

	m.n_out = 0;
	T_EQ_INT(fpal_enroll(dev, &new_id, 0, NULL, NULL), FPAL_E_FULL);
	T_EQ_INT(m.n_out, 1);
	T_CHECK(out_is(&m, 0, &egis_cmd_list));
	fpal_device_close(dev);
}

static void test_existing_id_is_refused(void) {
	struct fpal_template_id before[2];
	struct fpal_device *dev;
	struct mock m;

	make_ids(before, 2);
	before[1] = new_id;
	mock_init(&m);
	dev = open_dev(&m);
	push_list(&m, before, 2);

	m.n_out = 0;
	T_EQ_INT(fpal_enroll(dev, &new_id, 0, NULL, NULL), FPAL_E_EXISTS);
	T_EQ_INT(m.n_out, 1);
	fpal_device_close(dev);
}

static void test_cancel_never_stores(void) {
	struct fpal_template_id before[1];
	struct fpal_device *dev;
	struct mock m;

	make_ids(before, 1);
	mock_init(&m);
	dev = open_dev(&m);
	push_prefix(&m, before, 1, EGIS_SW_NOMATCH);
	push_scans(&m, STAGES);
	push_ok(&m);
	m.irq_fail_at = 6; /* the finger wait of the fifth scan */
	m.irq_fail_rc = FPAL_E_CANCELLED;

	m.n_out = 0;
	T_EQ_INT(fpal_enroll(dev, &new_id, 0, NULL, NULL), FPAL_E_CANCELLED);
	T_EQ_INT(writes_sent(&m), 0);
	T_CHECK(out_is(&m, m.n_out - 1, &egis_cmd_sensor_reset));
	fpal_device_close(dev);
}

static void test_too_many_failed_scans_give_up(void) {
	struct fpal_device *dev;
	struct mock m;

	mock_init(&m);
	dev = open_dev_pid(&m, 0x0582); /* 10 stages: keeps the script short */
	push_prefix(&m, NULL, 0, EGIS_SW_NOMATCH);
	for (unsigned i = 0; i < 10 + FPAL_ENROLL_MAX_RETRIES; i++)
		push_scan(&m, SCAN_CENTER);
	push_ok(&m); /* the loop pushes one scan per attempt, then gives up */
	push_ok(&m);
	push_ok(&m);

	m.n_out = 0;
	T_EQ_INT(fpal_enroll(dev, &new_id, 0, NULL, NULL), FPAL_E_TIMEOUT);
	T_EQ_INT(writes_sent(&m), 0);
	fpal_device_close(dev);
}

static void test_commit_not_listed_is_an_error(void) {
	struct fpal_template_id before[1];
	struct fpal_device *dev;
	struct mock m;

	make_ids(before, 1);
	mock_init(&m);
	dev = open_dev(&m);
	push_prefix(&m, before, 1, EGIS_SW_NOMATCH);
	push_scans(&m, STAGES);
	push_ok(&m);
	push_ok(&m);
	push_ok(&m);
	push_list(&m, before, 1); /* the new id did not show up */

	T_EQ_INT(fpal_enroll(dev, &new_id, 0, NULL, NULL), FPAL_E_PROTO);
	fpal_device_close(dev);
}

static void test_store_command_bytes(void) {
	uint8_t out[64];
	size_t n;
	static const uint8_t head[] = { 0x00, 0x00, 0x00, 0x27, 0x50, 0x16,
					0x03, 0x00, 0x00, 0x00, 0x20 };

	T_EQ_INT(egis_build_commit(&new_id, out, sizeof(out), &n), 0);
	T_EQ_INT(n, sizeof(head) + FPAL_TEMPLATE_ID_LEN);
	T_EQ_MEM(out, head, sizeof(head));
	T_EQ_MEM(out + sizeof(head), new_id.bytes, FPAL_TEMPLATE_ID_LEN);
	T_EQ_INT(egis_build_commit(&new_id, out, 10, &n), FPAL_E_OVERFLOW);
}

static void test_enrol_command_bytes(void) {
	static const uint8_t arm[] = { 0, 0, 0, 4, 0x50, 0x17, 0x01, 0x00 };
	static const uint8_t start[] = { 0, 0, 0, 7, 0x50, 0x16, 0x01, 0, 0, 0,
					 0x20 };
	static const uint8_t cap[] = { 0, 0, 0, 4, 0x50, 0x16, 0x02, 0x01 };
	static const uint8_t read[] = { 0, 0, 0, 7, 0x50, 0x16, 0x02, 0x02, 0,
					0, 0x02 };
	static const uint8_t commit[] = { 0, 0, 0, 7, 0x50, 0x16, 0x05, 0, 0,
					  0, 0x20 };

	T_EQ_MEM(egis_cmd_arm_enroll.data, arm, sizeof(arm));
	T_EQ_MEM(egis_cmd_enroll_start.data, start, sizeof(start));
	T_EQ_MEM(egis_cmd_start_capture.data, cap, sizeof(cap));
	T_EQ_MEM(egis_cmd_read_capture.data, read, sizeof(read));
	T_EQ_MEM(egis_cmd_commit_start.data, commit, sizeof(commit));
}

static void test_classify_capture(void) {
	struct egis_rsp r = { .body_len = 2, .sw = 0x9000 };

	T_EQ_INT(egis_classify_capture(&r), EGIS_CAPTURE_OK);
	r.sw = 0x6491;
	T_EQ_INT(egis_classify_capture(&r), EGIS_CAPTURE_CENTER);
	r.body_len = 0;
	r.sw = 0x6400;
	T_EQ_INT(egis_classify_capture(&r), EGIS_CAPTURE_DIRTY);
	r.sw = 0x6a80;
	T_EQ_INT(egis_classify_capture(&r), EGIS_CAPTURE_RETRY);
	r.body_len = 2;
	r.sw = 0x6493; /* seen on 1c7a:0587, meaning unknown */
	T_EQ_INT(egis_classify_capture(&r), EGIS_CAPTURE_RETRY);
	r.sw = 0x6495;
	T_EQ_INT(egis_classify_capture(&r), EGIS_CAPTURE_RETRY);
	r.sw = 0x9004;
	T_EQ_INT(egis_classify_capture(&r), EGIS_CAPTURE_RETRY);
}

static void test_generated_ids(void) {
	struct fpal_template_id a, b;

	T_EQ_INT(fpal_template_id_generate(&a), FPAL_OK);
	T_EQ_INT(fpal_template_id_generate(&b), FPAL_OK);
	T_EQ_MEM(a.bytes, FPAL_NEW_ID_PREFIX, sizeof(FPAL_NEW_ID_PREFIX) - 1);
	for (size_t i = 0; i < FPAL_TEMPLATE_ID_LEN; i++)
		T_CHECK(a.bytes[i] >= 0x20 && a.bytes[i] <= 0x7e);
	T_CHECK(memcmp(a.bytes, b.bytes, FPAL_TEMPLATE_ID_LEN) != 0);
}

/* ---- delete -------------------------------------------------------------- */

static unsigned deletes_sent(const struct mock *m) {
	unsigned n = 0;

	for (size_t i = 0; i < m->n_out; i++)
		if (out_payload(m, i)[4] == 0x50 && out_ins(m, i) == 0x18)
			n++;
	return n;
}

static void test_delete_command_bytes(void) {
	static const uint8_t head[] = { 0x00, 0x00, 0x00, 0x27, 0x50, 0x18,
					0x04, 0x00, 0x00, 0x00, 0x20 };
	uint8_t out[64];
	size_t n;

	T_EQ_INT(egis_build_delete(&new_id, out, sizeof(out), &n), 0);
	T_EQ_INT(n, sizeof(head) + FPAL_TEMPLATE_ID_LEN); /* one id, 43 bytes */
	T_EQ_MEM(out, head, sizeof(head));
	T_EQ_MEM(out + sizeof(head), new_id.bytes, FPAL_TEMPLATE_ID_LEN);
	T_EQ_INT(egis_build_delete(&new_id, out, 10, &n), FPAL_E_OVERFLOW);
	T_EQ_INT(egis_build_delete(NULL, out, sizeof(out), &n), FPAL_E_INVAL);
}

static void test_delete_one_template(void) {
	struct fpal_template_id ids[3], rest[2];
	struct fpal_device *dev;
	uint8_t payload[64];
	struct mock m;
	size_t plen;

	make_ids(ids, 3);
	rest[0] = ids[0];
	rest[1] = ids[2];
	mock_init(&m);
	dev = open_dev(&m);
	push_list(&m, ids, 3);
	push_ok(&m);
	push_list(&m, rest, 2);

	m.n_out = 0;
	T_EQ_INT(fpal_delete(dev, &ids[1]), FPAL_OK);
	T_EQ_INT(m.n_out, 3);
	T_CHECK(out_is(&m, 0, &egis_cmd_list));
	T_EQ_INT(egis_build_delete(&ids[1], payload, sizeof(payload), &plen), 0);
	T_EQ_INT(m.out[1].len, EGIS_HDR_LEN + plen);
	T_EQ_MEM(out_payload(&m, 1), payload, plen);
	T_CHECK(out_is(&m, 2, &egis_cmd_list));
	T_EQ_INT(deletes_sent(&m), 1);
	fpal_device_close(dev);
}

static void test_delete_unknown_id_sends_nothing(void) {
	struct fpal_template_id ids[2];
	struct fpal_device *dev;
	struct mock m;

	make_ids(ids, 2);
	mock_init(&m);
	dev = open_dev(&m);
	push_list(&m, ids, 2);

	m.n_out = 0;
	T_EQ_INT(fpal_delete(dev, &new_id), FPAL_E_NODATA);
	T_EQ_INT(deletes_sent(&m), 0);
	fpal_device_close(dev);
}

static void test_delete_refused_by_the_sensor(void) {
	struct fpal_template_id ids[2];
	struct fpal_device *dev;
	struct mock m;

	make_ids(ids, 2);
	mock_init(&m);
	dev = open_dev(&m);
	push_list(&m, ids, 2);
	push_status(&m, 0x6a80);

	T_EQ_INT(fpal_delete(dev, &ids[0]), FPAL_E_PROTO);
	fpal_device_close(dev);
}

/* The read back must show exactly the requested template gone. */
static void test_delete_checks_what_really_happened(void) {
	struct fpal_template_id ids[3], other[3], d;
	struct fpal_device *dev;
	struct mock m;

	make_ids(ids, 3);
	make_ids(other, 3);
	memset(d.bytes, 'Z', FPAL_TEMPLATE_ID_LEN);

	/* nothing was removed */
	mock_init(&m);
	dev = open_dev(&m);
	push_list(&m, ids, 3);
	push_ok(&m);
	push_list(&m, ids, 3);
	T_EQ_INT(fpal_delete(dev, &ids[1]), FPAL_E_PROTO);
	fpal_device_close(dev);

	/* more than the requested one was removed */
	mock_init(&m);
	dev = open_dev(&m);
	push_list(&m, ids, 3);
	push_ok(&m);
	push_list(&m, &ids[0], 1);
	T_EQ_INT(fpal_delete(dev, &ids[1]), FPAL_E_PROTO);
	fpal_device_close(dev);

	/* the right count, but a different template vanished */
	other[1] = ids[0];
	other[0] = d;
	mock_init(&m);
	dev = open_dev(&m);
	push_list(&m, ids, 3);
	push_ok(&m);
	push_list(&m, other, 2); /* d and ids[0]: ids[2] is missing */
	T_EQ_INT(fpal_delete(dev, &ids[1]), FPAL_E_PROTO);
	fpal_device_close(dev);
}

int main(void) {
	T_RUN(test_full_enroll);
	T_RUN(test_dry_run_never_writes);
	T_RUN(test_retries_do_not_count);
	T_RUN(test_ten_stage_sensor);
	T_RUN(test_duplicate_finger_stops_before_enrol_mode);
	T_RUN(test_full_sensor_sends_nothing_but_list);
	T_RUN(test_existing_id_is_refused);
	T_RUN(test_cancel_never_stores);
	T_RUN(test_too_many_failed_scans_give_up);
	T_RUN(test_commit_not_listed_is_an_error);
	T_RUN(test_store_command_bytes);
	T_RUN(test_enrol_command_bytes);
	T_RUN(test_classify_capture);
	T_RUN(test_generated_ids);
	T_RUN(test_delete_command_bytes);
	T_RUN(test_delete_one_template);
	T_RUN(test_delete_unknown_id_sends_nothing);
	T_RUN(test_delete_refused_by_the_sensor);
	T_RUN(test_delete_checks_what_really_happened);
	return T_DONE();
}
