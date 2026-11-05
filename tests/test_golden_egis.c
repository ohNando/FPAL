// SPDX-License-Identifier: Apache-2.0
/*
 * Golden transcripts of the Egis driver: for each scenario the COMPLETE list
 * of frames the driver sends to the sensor, as hex, plus the result code and
 * the transport counters. The files in tests/golden/ were recorded from the
 * driver before the driver API was generalised and must never be regenerated
 * to make a refactor pass: they are the proof that the bytes on the wire did
 * not change.
 *
 *   build/tests/test_golden_egis            compare with the files in tests/golden
 *   build/tests/test_golden_egis --update   rewrite those files (deliberate changes only)
 */
#include <stdio.h>
#include <stdlib.h>

#include "egis_util.h"

#define GOLDEN_DIR "tests/golden"

static int update_mode;

/* ---- recording ----------------------------------------------------------- */

static void dump(FILE *f, const struct mock *m, int rc) {
	for (size_t i = 0; i < m->n_out; i++) {
		fprintf(f, "OUT %zu ", m->out[i].len);
		for (size_t b = 0; b < m->out[i].len; b++)
			fprintf(f, "%02x", m->out[i].data[b]);
		fputc('\n', f);
	}
	fprintf(f, "RESULT rc=%d irqs=%u resets=%u claims=%u releases=%u ctrls=%u\n",
		rc, m->irqs, m->resets, m->claims, m->releases, m->ctrls);
}

static bool read_file(const char *path, char **out) {
	FILE *f = fopen(path, "rb");
	long n;

	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	*out = malloc((size_t)n + 1);
	if (!*out || fread(*out, 1, (size_t)n, f) != (size_t)n) {
		fclose(f);
		return false;
	}
	(*out)[n] = '\0';
	fclose(f);
	return true;
}

/* Compare (or, with --update, store) the transcript of one scenario. */
static void check(const char *name, const struct mock *m, int rc) {
	char path[128], *want = NULL, *got = NULL;
	size_t len = 0;
	FILE *mem = open_memstream(&got, &len);

	dump(mem, m, rc);
	fclose(mem);
	snprintf(path, sizeof(path), GOLDEN_DIR "/%s.txt", name);

	if (update_mode) {
		FILE *f = fopen(path, "wb");

		T_CHECK(f != NULL);
		if (f) {
			fwrite(got, 1, len, f);
			fclose(f);
		}
	} else if (!read_file(path, &want)) {
		fprintf(stderr, "%s: golden file missing (run from the repository root)\n",
			path);
		T_CHECK(false);
	} else {
		const bool same = strcmp(want, got) == 0;

		if (!same)
			fprintf(stderr, "%s: the frames sent to the sensor changed\n", path);
		T_CHECK(same);
	}
	free(want);
	free(got);
}

/* ---- scripting helpers --------------------------------------------------- */

static void fill_ids(struct fpal_template_id *ids, size_t n) {
	make_ids(ids, n);
}

static void push_status(struct mock *m, uint16_t sw) {
	mock_push_egis(m, NULL, 0, sw);
}

/* identify match response: 32 bytes of metadata, then the matched id */
static void push_match(struct mock *m, const struct fpal_template_id *id) {
	uint8_t body[64];

	memset(body, 0x14, 32);
	memcpy(body + 32, id->bytes, EGIS_ID_LEN);
	mock_push_egis(m, body, sizeof(body), EGIS_SW_OK);
}

/* ---- list and identify --------------------------------------------------- */

static void scenario_list(uint16_t pid, const char *name) {
	struct fpal_template_id ids[3], got[FPAL_MAX_TEMPLATES];
	struct fpal_device *dev;
	struct mock m;
	size_t n = 0;
	int rc;

	fill_ids(ids, 3);
	mock_init(&m);
	dev = open_dev_pid(&m, pid);
	T_CHECK(dev != NULL);
	push_list(&m, ids, 3);
	rc = fpal_list(dev, got, FPAL_MAX_TEMPLATES, &n);
	T_EQ_INT(n, 3);
	check(name, &m, rc);
	fpal_device_close(dev);
}

static void run_identify(uint16_t pid, const char *name, int mode) {
	struct fpal_template_id ids[2], match;
	struct fpal_device *dev;
	struct mock m;
	int rc;

	fill_ids(ids, 2);
	mock_init(&m);
	dev = open_dev_pid(&m, pid);
	T_CHECK(dev != NULL);
	push_list(&m, ids, 2);
	push_ok(&m);		  /* reset */
	push_status(&m, 0x9100);  /* arm identify */
	if (mode == 0 || mode == 1) {
		push_ok(&m); /* sensor check */
		if (mode == 0)
			push_match(&m, &ids[1]);
		else
			push_status(&m, EGIS_SW_NOMATCH);
	} else {
		m.irq_rc = mode == 2 ? FPAL_E_TIMEOUT : FPAL_E_CANCELLED;
	}
	push_ok(&m); /* final reset */
	rc = fpal_identify(dev, 5000, &match);
	check(name, &m, rc);
	fpal_device_close(dev);
}

static void test_list_0587(void) { scenario_list(0x0587, "list_0587"); }
static void test_list_05a1(void) { scenario_list(0x05a1, "list_05a1"); }
static void test_identify_match(void) { run_identify(0x0587, "identify_match_0587", 0); }
static void test_identify_nomatch(void) { run_identify(0x0587, "identify_nomatch_0587", 1); }
static void test_identify_timeout(void) { run_identify(0x0587, "identify_timeout_0587", 2); }
static void test_identify_cancel(void) { run_identify(0x0587, "identify_cancel_0587", 3); }
/* the 05a1 sends a different check prefix; 0582 has 10 enrol stages */
static void test_identify_match_05a1(void) { run_identify(0x05a1, "identify_match_05a1", 0); }
static void test_identify_match_0582(void) { run_identify(0x0582, "identify_match_0582", 0); }

#ifdef FPAL_ENABLE_ENROLL
/* ---- enrol and delete ---------------------------------------------------- */

static const struct fpal_template_id new_id = {
	.bytes = "FPAL-0123456789abcdef0123456789"
};

static void push_scan(struct mock *m, int kind) {
	static const uint8_t two[2] = { 0x00, 0x01 };

	push_ok(m);		 /* reset */
	push_status(m, 0x9100);	 /* start capture */
	switch (kind) {
	case 1:
		mock_push_egis(m, two, sizeof(two), 0x6491); /* off centre */
		break;
	case 2:
		push_status(m, 0x6400); /* dirty */
		break;
	case 3:
		mock_push_egis(m, two, sizeof(two), 0x6493); /* seen on hardware */
		break;
	default:
		mock_push_egis(m, two, sizeof(two), 0x9000);
		break;
	}
}

/* list, reset, arm, sensor check, duplicate check, enrol start */
static void push_enrol_prefix(struct mock *m, const struct fpal_template_id *ids,
			      size_t n) {
	uint8_t token[32];

	memset(token, 0x5a, sizeof(token));
	push_list(m, ids, n);
	push_ok(m);
	push_status(m, 0x9100);
	push_ok(m);
	push_status(m, EGIS_SW_NOMATCH);
	mock_push_egis(m, token, sizeof(token), EGIS_SW_OK);
}

static void run_enrol(uint16_t pid, const char *name, unsigned stages, bool commit) {
	struct fpal_template_id ids[2], after[3];
	struct fpal_device *dev;
	struct mock m;
	int rc;

	fill_ids(ids, 2);
	memcpy(after, ids, sizeof(ids));
	after[2] = new_id;
	mock_init(&m);
	dev = open_dev_pid(&m, pid);
	T_CHECK(dev != NULL);
	push_enrol_prefix(&m, ids, 2);
	/* a few unreadable scans on the way: they must not count as stages */
	push_scan(&m, 1);
	push_scan(&m, 3);
	push_scan(&m, 2);
	for (unsigned i = 0; i < stages; i++)
		push_scan(&m, 0);
	if (commit) {
		push_ok(&m);		 /* commit start */
		push_ok(&m);		 /* store */
		push_ok(&m);		 /* final reset */
		push_list(&m, after, 3); /* read back */
	} else {
		push_ok(&m); /* final reset */
	}
	rc = fpal_enroll(dev, &new_id, commit ? 0 : FPAL_ENROLL_DRY_RUN, NULL, NULL);
	check(name, &m, rc);
	fpal_device_close(dev);
}

static void test_enrol_dry_0587(void) { run_enrol(0x0587, "enroll_dry_0587", 20, false); }
static void test_enrol_dry_0582(void) { run_enrol(0x0582, "enroll_dry_0582", 10, false); }
static void test_enrol_dry_05a1(void) { run_enrol(0x05a1, "enroll_dry_05a1", 10, false); }
static void test_enrol_real_0587(void) { run_enrol(0x0587, "enroll_real_0587", 20, true); }
static void test_enrol_real_05a1(void) { run_enrol(0x05a1, "enroll_real_05a1", 10, true); }

static void run_delete(const char *name, int mode) {
	struct fpal_template_id ids[3], rest[2], unknown;
	struct fpal_device *dev;
	struct mock m;
	int rc;

	fill_ids(ids, 3);
	rest[0] = ids[0];
	rest[1] = ids[2];
	unknown = new_id;
	mock_init(&m);
	dev = open_dev(&m);
	T_CHECK(dev != NULL);
	push_list(&m, ids, 3);
	if (mode == 0) {
		push_ok(&m);
		push_list(&m, rest, 2);
	} else if (mode == 1) {
		push_status(&m, 0x6a80);
	}
	rc = fpal_delete(dev, mode == 2 ? &unknown : &ids[1]);
	check(name, &m, rc);
	fpal_device_close(dev);
}

static void test_delete_ok(void) { run_delete("delete_ok_0587", 0); }
static void test_delete_refused(void) { run_delete("delete_refused_0587", 1); }
static void test_delete_unknown(void) { run_delete("delete_unknown_0587", 2); }
#endif

int main(int argc, char **argv) {
	update_mode = argc > 1 && !strcmp(argv[1], "--update");

	T_RUN(test_list_0587);
	T_RUN(test_list_05a1);
	T_RUN(test_identify_match);
	T_RUN(test_identify_nomatch);
	T_RUN(test_identify_timeout);
	T_RUN(test_identify_cancel);
	T_RUN(test_identify_match_05a1);
	T_RUN(test_identify_match_0582);
#ifdef FPAL_ENABLE_ENROLL
	T_RUN(test_enrol_dry_0587);
	T_RUN(test_enrol_dry_0582);
	T_RUN(test_enrol_dry_05a1);
	T_RUN(test_enrol_real_0587);
	T_RUN(test_enrol_real_05a1);
	T_RUN(test_delete_ok);
	T_RUN(test_delete_refused);
	T_RUN(test_delete_unknown);
#endif
	return T_DONE();
}
