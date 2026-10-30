/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Tiny self-contained test harness: a handful of macros, no library.
 */
#ifndef FPX_TESTS_T_H
#define FPX_TESTS_T_H

#include <stdio.h>
#include <string.h>

static unsigned t_failed;
static unsigned t_run;

#define T_CHECK(cond)                                                   \
	do {                                                            \
		if (!(cond)) {                                          \
			fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__,  \
				__LINE__, #cond);                       \
			t_failed++;                                     \
		}                                                       \
	} while (0)

#define T_EQ_INT(a, b)                                                  \
	do {                                                            \
		long long t_a_ = (long long)(a), t_b_ = (long long)(b); \
		if (t_a_ != t_b_) {                                     \
			fprintf(stderr, "%s:%d: FAIL: %s == %s (%lld != %lld)\n", \
				__FILE__, __LINE__, #a, #b, t_a_, t_b_); \
			t_failed++;                                     \
		}                                                       \
	} while (0)

#define T_EQ_MEM(a, b, n) T_CHECK(memcmp((a), (b), (n)) == 0)

#define T_RUN(fn)                                                       \
	do {                                                            \
		unsigned t_before_ = t_failed;                          \
		t_run++;                                                \
		fn();                                                   \
		printf("  %-40s %s\n", #fn,                             \
		       t_failed == t_before_ ? "ok" : "FAILED");        \
	} while (0)

#define T_DONE()                                                        \
	(printf("%u test(s), %u failure(s)\n", t_run, t_failed),        \
	 t_failed ? 1 : 0)

#endif /* FPX_TESTS_T_H */
