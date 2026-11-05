/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Minimal logging. Messages go to stderr by default; the PAM module and the
 * daemon install their own sink (syslog) with fpal_log_set_sink().
 */
#ifndef FPAL_CORE_LOG_H
#define FPAL_CORE_LOG_H

#include <stdarg.h>
#include <stdbool.h>

enum fpal_log_level {
	FPAL_LOG_ERR = 0,
	FPAL_LOG_WARN = 1,
	FPAL_LOG_INFO = 2,
	FPAL_LOG_DEBUG = 3,
};

typedef void (*fpal_log_sink)(enum fpal_log_level level, const char *msg);

void fpal_log_set_sink(fpal_log_sink sink);

/* Force debug messages on, regardless of FPAL_DEBUG. */
void fpal_log_set_debug(bool on);

/*
 * Whether FPAL_DEBUG is read at all. pam_fpal turns it off: it runs inside
 * programs like sudo whose environment belongs to the user who started them.
 */
void fpal_log_use_env(bool on);

void fpal_log(enum fpal_log_level level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

#define fpal_err(...) fpal_log(FPAL_LOG_ERR, __VA_ARGS__)
#define fpal_warn(...) fpal_log(FPAL_LOG_WARN, __VA_ARGS__)
#define fpal_info(...) fpal_log(FPAL_LOG_INFO, __VA_ARGS__)
#define fpal_dbg(...) fpal_log(FPAL_LOG_DEBUG, __VA_ARGS__)

#endif /* FPAL_CORE_LOG_H */
