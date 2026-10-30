// SPDX-License-Identifier: Apache-2.0
/*
 * pam_fpx: fingerprint authentication on top of libfpx.
 *
 *   auth sufficient pam_fpx.so [timeout=ms] [retries=n] [debug] [quiet]
 *
 * The user passes if the sensor identifies a finger whose template id is
 * bound to them in FPX_STATE_DIR. Every failure that is not "wrong finger"
 * (no bindings, no sensor, sensor busy, remote login, timeout) returns
 * PAM_AUTHINFO_UNAVAIL so the stack falls through to the password quickly.
 *
 * Messages are shown even under PAM_SILENT: sudo sets it by default, and
 * without the prompt the user cannot know a finger is expected. Use the
 * "quiet" option to suppress them.
 */
#define PAM_SM_AUTH

#include <security/pam_ext.h>
#include <security/pam_modules.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "core/log.h"
#include "fpx/bindings.h"
#include "fpx/fpx.h"
#include "opts.h"

#define MSG_RETRY "Fingerprint not recognised, try again"
#define MSG_FAIL "Fingerprint not recognised"
#define MSG_TIMEOUT "No finger detected"

/* libfpx log messages end up in the auth log. */
static void log_to_syslog(enum fpx_log_level level, const char *msg) {
	static const int prio[] = { LOG_ERR, LOG_WARNING, LOG_INFO,
				    LOG_DEBUG };

	syslog(LOG_AUTHPRIV | prio[level], "pam_fpx: %s", msg);
}

/* Fingerprints prove presence at this machine, not over the network. */
static bool is_remote(pam_handle_t *pamh) {
	const void *rhost = NULL;

	if (pam_get_item(pamh, PAM_RHOST, &rhost) != PAM_SUCCESS || !rhost)
		return false;
	return ((const char *)rhost)[0] &&
	       strcmp(rhost, "localhost") != 0;
}

static bool is_bound(const struct fpx_template_id *ids, size_t n,
		     const struct fpx_template_id *id) {
	for (size_t i = 0; i < n; i++)
		if (memcmp(ids[i].bytes, id->bytes, FPX_TEMPLATE_ID_LEN) == 0)
			return true;
	return false;
}

/* "[sudo] fingerprint for ohnando:" */
static void format_prompt(pam_handle_t *pamh, const char *user, char *buf,
			  size_t cap) {
	const void *service = NULL;

	if (pam_get_item(pamh, PAM_SERVICE, &service) != PAM_SUCCESS ||
	    !service || !((const char *)service)[0])
		service = "pam_fpx";
	snprintf(buf, cap, FPX_PAM_PROMPT_FMT, (const char *)service, user);
}

static int identify_user(pam_handle_t *pamh, const struct pam_fpx_opts *o,
			 const char *user, const struct fpx_template_id *ids,
			 size_t n) {
	char prompt[128];

	struct fpx_device *dev;
	int err;

	format_prompt(pamh, user, prompt, sizeof(prompt));
	err = fpx_device_open_first(&dev);
	if (err) {
		pam_syslog(pamh, LOG_NOTICE, "cannot open sensor: %s",
			   fpx_strerror(err));
		return PAM_AUTHINFO_UNAVAIL;
	}

	for (uint8_t attempt = 0; attempt < o->retries; attempt++) {
		struct fpx_template_id match;

		if (!o->quiet)
			pam_info(pamh, "%s", prompt);

		err = fpx_identify(dev, o->timeout_ms, &match);
		if (err == FPX_OK && is_bound(ids, n, &match)) {
			fpx_device_close(dev);
			pam_syslog(pamh, LOG_INFO,
				   "fingerprint accepted for %s", user);
			return PAM_SUCCESS;
		}
		if (err != FPX_OK && err != FPX_E_NOMATCH) {
			/* Timeout, cancel or I/O error: let the password in. */
			fpx_device_close(dev);
			if (!o->quiet && err == FPX_E_TIMEOUT)
				pam_error(pamh, MSG_TIMEOUT);
			if (o->debug)
				pam_syslog(pamh, LOG_DEBUG, "identify: %s",
					   fpx_strerror(err));
			return PAM_AUTHINFO_UNAVAIL;
		}

		/* Unknown finger, or someone else's. */
		pam_syslog(pamh, LOG_NOTICE,
			   "fingerprint rejected for %s (attempt %u/%u)", user,
			   attempt + 1u, o->retries);
		if (!o->quiet)
			pam_error(pamh, attempt + 1u < o->retries ? MSG_RETRY :
								     MSG_FAIL);
	}

	fpx_device_close(dev);
	return PAM_AUTH_ERR;
}

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc,
				   const char **argv) {
	struct fpx_template_id ids[FPX_MAX_TEMPLATES];
	struct pam_fpx_opts o;
	const char *user = NULL;
	size_t n = 0;
	int bad = -1, err;

	(void)flags; /* PAM_SILENT deliberately ignored, see above */
	if (pam_fpx_parse_opts(argc, argv, &o, &bad))
		pam_syslog(pamh, LOG_WARNING, "ignoring bad option '%s'",
			   argv[bad]);

	fpx_log_set_sink(log_to_syslog);
	fpx_log_set_debug(o.debug);

	if (pam_get_user(pamh, &user, NULL) != PAM_SUCCESS || !user)
		return PAM_USER_UNKNOWN;
	if (!fpx_bindings_valid_user(user))
		return PAM_AUTHINFO_UNAVAIL;

	if (is_remote(pamh)) {
		if (o.debug)
			pam_syslog(pamh, LOG_DEBUG, "remote login, skipping");
		return PAM_AUTHINFO_UNAVAIL;
	}

	/* No bindings: do not make the user wait for a finger. */
	err = fpx_bindings_load(FPX_STATE_DIR, user, ids, FPX_MAX_TEMPLATES,
				&n);
	if (err || n == 0) {
		if (o.debug)
			pam_syslog(pamh, LOG_DEBUG, "%s: no bindings (%s)",
				   user, fpx_strerror(err));
		return PAM_AUTHINFO_UNAVAIL;
	}

	return identify_user(pamh, &o, user, ids, n);
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags, int argc,
			      const char **argv) {
	(void)pamh;
	(void)flags;
	(void)argc;
	(void)argv;
	return PAM_SUCCESS;
}
