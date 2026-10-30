/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Minimal logging. Messages go to stderr by default; the PAM module and the
 * daemon install their own sink (syslog) with fpx_log_set_sink().
 */
#ifndef FPX_CORE_LOG_H
#define FPX_CORE_LOG_H

#include <stdarg.h>
#include <stdbool.h>

enum fpx_log_level {
	FPX_LOG_ERR = 0,
	FPX_LOG_WARN = 1,
	FPX_LOG_INFO = 2,
	FPX_LOG_DEBUG = 3,
};

typedef void (*fpx_log_sink)(enum fpx_log_level level, const char *msg);

void fpx_log_set_sink(fpx_log_sink sink);

/* Force debug messages on, regardless of FPX_DEBUG. */
void fpx_log_set_debug(bool on);

void fpx_log(enum fpx_log_level level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

#define fpx_err(...) fpx_log(FPX_LOG_ERR, __VA_ARGS__)
#define fpx_warn(...) fpx_log(FPX_LOG_WARN, __VA_ARGS__)
#define fpx_info(...) fpx_log(FPX_LOG_INFO, __VA_ARGS__)
#define fpx_dbg(...) fpx_log(FPX_LOG_DEBUG, __VA_ARGS__)

#endif /* FPX_CORE_LOG_H */
