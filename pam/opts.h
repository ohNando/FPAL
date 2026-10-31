/* SPDX-License-Identifier: Apache-2.0 */
/*
 * pam_fpal module options, as written in /etc/pam.d/<service>:
 *
 *   auth sufficient pam_fpal.so timeout=10000 retries=3 debug quiet
 */
#ifndef FPAL_PAM_OPTS_H
#define FPAL_PAM_OPTS_H

#include "fpal/fpal.h"

struct pam_fpal_opts {
	uint32_t timeout_ms;	/* finger wait per attempt */
	uint8_t retries;	/* identify attempts */
	bool debug;		/* log to syslog at debug level */
	bool quiet;		/* no prompts or result messages */
};

/*
 * Parse module arguments into @o. Unknown or invalid arguments are reported
 * through @bad (index into @argv) and FPAL_E_INVAL is returned; @o then still
 * holds usable values (defaults for the rejected options).
 */
int pam_fpal_parse_opts(int argc, const char **argv, struct pam_fpal_opts *o,
		       int *bad);

#endif /* FPAL_PAM_OPTS_H */
