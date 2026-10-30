// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <string.h>

#include "opts.h"

/* Parse "<key><number>" with @min <= number <= @max. */
static bool parse_num(const char *arg, const char *key, unsigned long min,
		      unsigned long max, unsigned long *out) {
	const size_t klen = strlen(key);
	unsigned long v;
	char *end;

	if (strncmp(arg, key, klen) != 0 || !arg[klen])
		return false;
	if (arg[klen] < '0' || arg[klen] > '9')
		return false;
	v = strtoul(arg + klen, &end, 10);
	if (*end || v < min || v > max)
		return false;
	*out = v;
	return true;
}

int pam_fpx_parse_opts(int argc, const char **argv, struct pam_fpx_opts *o,
		       int *bad) {
	int err = FPX_OK;

	o->timeout_ms = FPX_PAM_TIMEOUT_MS;
	o->retries = FPX_PAM_RETRIES;
	o->debug = false;
	o->quiet = false;

	for (int i = 0; i < argc; i++) {
		const char *a = argv[i];
		unsigned long v;

		if (strcmp(a, "debug") == 0) {
			o->debug = true;
		} else if (strcmp(a, "quiet") == 0) {
			o->quiet = true;
		} else if (parse_num(a, "timeout=", 1, FPX_FINGER_WAIT_TIMEOUT_MS,
				     &v)) {
			o->timeout_ms = (uint32_t)v;
		} else if (parse_num(a, "retries=", 1, FPX_PAM_RETRIES_MAX,
				     &v)) {
			o->retries = (uint8_t)v;
		} else {
			if (bad && err == FPX_OK)
				*bad = i;
			err = FPX_E_INVAL;
		}
	}
	return err;
}
