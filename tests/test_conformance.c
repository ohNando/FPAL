// SPDX-License-Identifier: Apache-2.0
/*
 * What every driver has to do, shown on a made-up sensor (tests/drivers/
 * fake_moc.c). The real Egis driver has its own exact-byte tests; these check
 * the behaviour the core promises whatever the sensor: balanced claims,
 * a final reset, no write before the safety checks, and trust in the chip's
 * own list after a write.
 */
#include <string.h>

#include "driver_lint.h"
#include "drivers/fake_moc.h"
#include "id_util.h"
#include "t.h"

static struct fake_chip chip;
static struct fpal_device *dev;

static bool begin(void) {
	fake_chip_init(&chip);
	dev = NULL;
	return true;
}

static bool open_chip(void) {
	return fake_chip_open(&chip, &dev) == FPAL_OK && dev != NULL;
}

static void finish(void) {
	fpal_device_close(dev);
	dev = NULL;
}

static bool sent(uint8_t cmd) {
	for (size_t i = 0; i < chip.n_cmds; i++)
		if (chip.cmds[i] == cmd)
			return true;
	return false;
}

static void test_the_driver_follows_the_rules(void) {
	driver_lint(&fpal_driver_fake_moc);
	T_EQ_INT(fpal_driver_fake_moc.max_templates, FAKE_MAX_TEMPLATES);
}

static void test_open_close_balance_claims(void) {
	begin();
	T_CHECK(open_chip());
	T_EQ_INT(chip.claims, 1);
	T_EQ_INT(chip.ctrl_outs, 1); /* the power-on control-out was sent */
	T_CHECK(!strcmp(fpal_device_fw_version(dev), "FK01"));
	T_EQ_INT(fpal_device_max_templates(dev), FAKE_MAX_TEMPLATES);
	T_EQ_INT(fpal_device_new_id_len(dev), FAKE_ID_LEN);
	finish();
	T_EQ_INT(chip.releases, chip.claims);
}

static void test_list(void) {
	struct fpal_template_id ids[FAKE_MAX_TEMPLATES], want;
	size_t n = 9;

	begin();
	T_CHECK(open_chip());
	T_EQ_INT(fpal_list(dev, ids, FAKE_MAX_TEMPLATES, &n), FPAL_OK);
	T_EQ_INT(n, 0);
	fake_chip_add(&chip, 0x11);
	fake_chip_add(&chip, 0x22);
	T_EQ_INT(fpal_list(dev, ids, FAKE_MAX_TEMPLATES, &n), FPAL_OK);
	T_EQ_INT(n, 2);
	T_EQ_INT(fpal_id_len(&ids[0]), FAKE_ID_LEN);
	fake_chip_id(0x22, &want);
	T_CHECK(fpal_id_equal(&ids[1], &want));
	T_EQ_INT(fpal_list(dev, ids, 1, &n), FPAL_E_OVERFLOW); /* caller's room */
	finish();
}

static void test_identify_reports_an_id_from_the_list(void) {
	struct fpal_template_id match, want;

	begin();
	fake_chip_add(&chip, 0x11);
	fake_chip_add(&chip, 0x22);
	fake_chip_touch_stored(&chip, 1);
	T_CHECK(open_chip());
	T_EQ_INT(fpal_identify(dev, 1000, &match), FPAL_OK);
	fake_chip_id(0x22, &want);
	T_CHECK(fpal_id_equal(&match, &want));
	T_EQ_INT(chip.cmds[chip.n_cmds - 1], FAKE_CMD_RESET); /* left idle */
	finish();
}

static void test_identify_other_outcomes(void) {
	struct fpal_template_id match;

	begin();
	T_CHECK(open_chip());
	T_EQ_INT(fpal_identify(dev, 1000, &match), FPAL_E_NODATA); /* empty chip */
	T_CHECK(!sent(FAKE_CMD_ARM_IDENTIFY)); /* nothing to look for: no scan */
	finish();

	begin();
	fake_chip_add(&chip, 0x11);
	fake_chip_touch_unknown(&chip);
	T_CHECK(open_chip());
	T_EQ_INT(fpal_identify(dev, 1000, &match), FPAL_E_NOMATCH);
	T_EQ_INT(chip.cmds[chip.n_cmds - 1], FAKE_CMD_RESET);
	finish();

	begin();
	fake_chip_add(&chip, 0x11); /* no finger ever arrives */
	T_CHECK(open_chip());
	T_EQ_INT(fpal_identify(dev, 1000, &match), FPAL_E_TIMEOUT);
	T_EQ_INT(chip.cmds[chip.n_cmds - 1], FAKE_CMD_RESET);
	finish();

	begin();
	fake_chip_add(&chip, 0x11);
	T_CHECK(open_chip());
	fpal_cancel(dev); /* Ctrl-C before the finger */
	T_EQ_INT(fpal_identify(dev, 1000, &match), FPAL_E_CANCELLED);
	T_EQ_INT(chip.cmds[chip.n_cmds - 1], FAKE_CMD_RESET);
	finish();
}

static void test_a_chip_that_talks_nonsense_cannot_hurt(void) {
	struct fpal_template_id ids[FAKE_MAX_TEMPLATES], match;
	size_t n = 0;

	begin();
	fake_chip_add(&chip, 0x11);
	chip.garbage = true;
	T_CHECK(open_chip());
	T_EQ_INT(fpal_list(dev, ids, FAKE_MAX_TEMPLATES, &n), FPAL_E_PROTO);
	T_EQ_INT(fpal_identify(dev, 1000, &match), FPAL_E_PROTO);
	finish();
	T_EQ_INT(chip.releases, chip.claims);
}

static void test_a_matched_id_the_chip_does_not_hold_is_refused(void) {
	struct fpal_template_id match;
	uint8_t bogus[FAKE_ID_LEN];

	begin();
	fake_chip_add(&chip, 0x11);
	memset(bogus, 0x99, sizeof(bogus));
	chip.events[0][0] = FAKE_EVT_MATCH;
	memcpy(chip.events[0] + 1, bogus, sizeof(bogus));
	chip.event_len[0] = 1 + sizeof(bogus);
	chip.n_events = 1;
	T_CHECK(open_chip());
	T_EQ_INT(fpal_identify(dev, 1000, &match), FPAL_E_PROTO);
	finish();
}

#ifdef FPAL_ENABLE_ENROLL
static bool has_template(uint8_t seed) {
	struct fpal_template_id id, got[FAKE_MAX_TEMPLATES];
	size_t n = 0;

	fake_chip_id(seed, &id);
	if (fpal_list(dev, got, FAKE_MAX_TEMPLATES, &n) != FPAL_OK)
		return false;
	for (size_t i = 0; i < n; i++)
		if (fpal_id_equal(&got[i], &id))
			return true;
	return false;
}

static void test_enroll_stores_and_reads_back(void) {
	struct fpal_template_id id;

	begin();
	fake_chip_add(&chip, 0x11);
	fake_chip_script_enroll(&chip, false);
	T_CHECK(open_chip());
	T_EQ_INT(fpal_template_id_new(dev, &id), FPAL_OK);
	T_EQ_INT(fpal_id_len(&id), FAKE_ID_LEN);
	T_EQ_INT(fpal_enroll(dev, &id, 0, NULL, NULL), FPAL_OK);
	T_EQ_INT(chip.n, 2);
	/* the core looked first and read back at the end */
	T_EQ_INT(chip.cmds[0], FAKE_CMD_LIST);
	T_EQ_INT(chip.cmds[1], FAKE_CMD_ENROLL_BEGIN);
	T_EQ_INT(chip.cmds[chip.n_cmds - 2], FAKE_CMD_RESET);
	T_EQ_INT(chip.cmds[chip.n_cmds - 1], FAKE_CMD_LIST);
	finish();
}

static void test_enroll_dry_run_stores_nothing(void) {
	struct fpal_template_id id;

	begin();
	fake_chip_script_enroll(&chip, false);
	T_CHECK(open_chip());
	fake_chip_id(0x77, &id);
	T_EQ_INT(fpal_enroll(dev, &id, FPAL_ENROLL_DRY_RUN, NULL, NULL), FPAL_OK);
	T_CHECK(!sent(FAKE_CMD_STORE));
	T_EQ_INT(chip.n, 0);
	T_EQ_INT(chip.cmds[chip.n_cmds - 1], FAKE_CMD_RESET); /* no read-back */
	finish();
}

static void test_enroll_refuses_before_touching_the_chip(void) {
	struct fpal_template_id id;

	begin(); /* full */
	for (uint8_t s = 1; s <= FAKE_MAX_TEMPLATES; s++)
		fake_chip_add(&chip, s);
	T_CHECK(open_chip());
	fake_chip_id(0x77, &id);
	T_EQ_INT(fpal_enroll(dev, &id, 0, NULL, NULL), FPAL_E_FULL);
	T_CHECK(!sent(FAKE_CMD_ENROLL_BEGIN));
	finish();

	begin(); /* id already there */
	fake_chip_add(&chip, 0x11);
	T_CHECK(open_chip());
	fake_chip_id(0x11, &id);
	T_EQ_INT(fpal_enroll(dev, &id, 0, NULL, NULL), FPAL_E_EXISTS);
	T_CHECK(!sent(FAKE_CMD_ENROLL_BEGIN));
	finish();
}

static void test_enroll_failures_leave_the_chip_alone(void) {
	struct fpal_template_id id;

	fake_chip_id(0x77, &id);

	begin(); /* the chip knows this finger */
	fake_chip_add(&chip, 0x11);
	fake_chip_script_enroll(&chip, true);
	T_CHECK(open_chip());
	T_EQ_INT(fpal_enroll(dev, &id, 0, NULL, NULL), FPAL_E_EXISTS);
	T_CHECK(!sent(FAKE_CMD_STORE));
	T_EQ_INT(chip.n, 1);
	T_EQ_INT(chip.cmds[chip.n_cmds - 1], FAKE_CMD_RESET);
	finish();

	begin(); /* nobody touches the sensor */
	T_CHECK(open_chip());
	T_EQ_INT(fpal_enroll(dev, &id, 0, NULL, NULL), FPAL_E_TIMEOUT);
	T_CHECK(!sent(FAKE_CMD_STORE));
	T_EQ_INT(chip.cmds[chip.n_cmds - 1], FAKE_CMD_RESET);
	finish();
}

static void test_enroll_does_not_trust_a_status_word(void) {
	struct fpal_template_id id;

	begin();
	chip.lie_store = true; /* says OK, keeps nothing */
	fake_chip_script_enroll(&chip, false);
	T_CHECK(open_chip());
	fake_chip_id(0x77, &id);
	T_EQ_INT(fpal_enroll(dev, &id, 0, NULL, NULL), FPAL_E_PROTO);
	finish();
}

static void test_delete_removes_exactly_one(void) {
	struct fpal_template_id id;

	begin();
	fake_chip_add(&chip, 0x11);
	fake_chip_add(&chip, 0x22);
	fake_chip_add(&chip, 0x33);
	T_CHECK(open_chip());
	fake_chip_id(0x22, &id);
	T_EQ_INT(fpal_delete(dev, &id), FPAL_OK);
	T_EQ_INT(chip.n, 2);
	T_CHECK(has_template(0x11) && has_template(0x33) && !has_template(0x22));
	finish();
}

static void test_delete_checks_before_and_after(void) {
	struct fpal_template_id id;

	begin(); /* unknown id: the delete command is never sent */
	fake_chip_add(&chip, 0x11);
	T_CHECK(open_chip());
	fake_chip_id(0x99, &id);
	T_EQ_INT(fpal_delete(dev, &id), FPAL_E_NODATA);
	T_CHECK(!sent(FAKE_CMD_DELETE));
	finish();

	begin(); /* a chip that acknowledges but does nothing */
	fake_chip_add(&chip, 0x11);
	chip.lie_delete = true;
	T_CHECK(open_chip());
	fake_chip_id(0x11, &id);
	T_EQ_INT(fpal_delete(dev, &id), FPAL_E_PROTO);
	finish();

	begin(); /* a chip that deletes one too many */
	fake_chip_add(&chip, 0x11);
	fake_chip_add(&chip, 0x22);
	fake_chip_add(&chip, 0x33);
	chip.delete_two = true;
	T_CHECK(open_chip());
	fake_chip_id(0x22, &id);
	T_EQ_INT(fpal_delete(dev, &id), FPAL_E_PROTO);
	finish();
}
#else
static void test_read_only_build_cannot_write(void) {
	struct fpal_template_id id;

	begin();
	T_CHECK(fpal_driver_fake_moc.enroll == NULL);
	T_CHECK(fpal_driver_fake_moc.delete_template == NULL);
	T_CHECK(open_chip());
	fake_chip_add(&chip, 0x11);
	fake_chip_id(0x11, &id);
	T_EQ_INT(fpal_enroll(dev, &id, 0, NULL, NULL), FPAL_E_NOTSUP);
	T_EQ_INT(fpal_delete(dev, &id), FPAL_E_NOTSUP);
	T_EQ_INT(fpal_template_id_new(dev, &id), FPAL_E_NOTSUP);
	T_CHECK(!sent(FAKE_CMD_STORE) && !sent(FAKE_CMD_DELETE) &&
		!sent(FAKE_CMD_ENROLL_BEGIN));
	T_EQ_INT(chip.n, 1);
	finish();
}
#endif

int main(void) {
	T_RUN(test_the_driver_follows_the_rules);
	T_RUN(test_open_close_balance_claims);
	T_RUN(test_list);
	T_RUN(test_identify_reports_an_id_from_the_list);
	T_RUN(test_identify_other_outcomes);
	T_RUN(test_a_chip_that_talks_nonsense_cannot_hurt);
	T_RUN(test_a_matched_id_the_chip_does_not_hold_is_refused);
#ifdef FPAL_ENABLE_ENROLL
	T_RUN(test_enroll_stores_and_reads_back);
	T_RUN(test_enroll_dry_run_stores_nothing);
	T_RUN(test_enroll_refuses_before_touching_the_chip);
	T_RUN(test_enroll_failures_leave_the_chip_alone);
	T_RUN(test_enroll_does_not_trust_a_status_word);
	T_RUN(test_delete_removes_exactly_one);
	T_RUN(test_delete_checks_before_and_after);
#else
	T_RUN(test_read_only_build_cannot_write);
#endif
	return T_DONE();
}
