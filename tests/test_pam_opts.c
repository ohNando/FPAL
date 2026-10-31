// SPDX-License-Identifier: Apache-2.0
#include "../pam/opts.h"
#include "t.h"

static void test_defaults(void) {
	struct pam_fpal_opts o;

	T_EQ_INT(pam_fpal_parse_opts(0, NULL, &o, NULL), FPAL_OK);
	T_EQ_INT(o.timeout_ms, FPAL_PAM_TIMEOUT_MS);
	T_EQ_INT(o.retries, FPAL_PAM_RETRIES);
	T_CHECK(!o.debug);
	T_CHECK(!o.quiet);
}

static void test_all_options(void) {
	const char *argv[] = { "timeout=2500", "retries=5", "debug", "quiet" };
	struct pam_fpal_opts o;

	T_EQ_INT(pam_fpal_parse_opts(4, argv, &o, NULL), FPAL_OK);
	T_EQ_INT(o.timeout_ms, 2500);
	T_EQ_INT(o.retries, 5);
	T_CHECK(o.debug);
	T_CHECK(o.quiet);
}

static void test_rejects_bad_values(void) {
	const char *cases[] = { "timeout=0", "timeout=", "timeout=-5",
				"timeout=12x", "timeout=999999999",
				"retries=0", "retries=11", "verbose" };
	struct pam_fpal_opts o;

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		int bad = -1;

		T_EQ_INT(pam_fpal_parse_opts(1, &cases[i], &o, &bad),
			 FPAL_E_INVAL);
		T_EQ_INT(bad, 0);
		T_EQ_INT(o.timeout_ms, FPAL_PAM_TIMEOUT_MS);
		T_EQ_INT(o.retries, FPAL_PAM_RETRIES);
	}
}

static void test_bad_index_is_first(void) {
	const char *argv[] = { "debug", "nope", "retries=99" };
	struct pam_fpal_opts o;
	int bad = -1;

	T_EQ_INT(pam_fpal_parse_opts(3, argv, &o, &bad), FPAL_E_INVAL);
	T_EQ_INT(bad, 1);
	T_CHECK(o.debug);
}

int main(void) {
	T_RUN(test_defaults);
	T_RUN(test_all_options);
	T_RUN(test_rejects_bad_values);
	T_RUN(test_bad_index_is_first);
	return T_DONE();
}
