/* SPDX-License-Identifier: Apache-2.0 */
/*
 * pam_fpx module options, as written in /etc/pam.d/<service>:
 *
 *   auth sufficient pam_fpx.so timeout=10000 retries=3 debug
 */
#ifndef FPX_PAM_OPTS_H
#define FPX_PAM_OPTS_H

#include "fpx/fpx.h"

struct pam_fpx_opts {
	uint32_t timeout_ms;	/* finger wait per attempt */
	uint8_t retries;	/* identify attempts */
	bool debug;		/* log to syslog at debug level */
};

/*
 * Parse module arguments into @o. Unknown or invalid arguments are reported
 * through @bad (index into @argv) and FPX_E_INVAL is returned; @o then still
 * holds usable values (defaults for the rejected options).
 */
int pam_fpx_parse_opts(int argc, const char **argv, struct pam_fpx_opts *o,
		       int *bad);

#endif /* FPX_PAM_OPTS_H */
