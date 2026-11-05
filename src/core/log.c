// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>

#include "fpal/config.h"
#include "log.h"

static fpal_log_sink log_sink;
static bool log_debug;
static bool log_env = true;

void fpal_log_set_sink(fpal_log_sink sink) {
	log_sink = sink;
}

void fpal_log_set_debug(bool on) {
	log_debug = on;
}

void fpal_log_use_env(bool on) {
	log_env = on;
}

static enum fpal_log_level max_level(void) {
	const char *env = log_env ? getenv(FPAL_ENV_DEBUG) : NULL;

	if (log_debug)
		return FPAL_LOG_DEBUG;
	return (env && env[0] && env[0] != '0') ? FPAL_LOG_DEBUG : FPAL_LOG_INFO;
}

void fpal_log(enum fpal_log_level level, const char *fmt, ...) {
	static const char *const prefix[] = { "error", "warning", "info",
					      "debug" };
	char msg[512];
	va_list ap;

	if (level > max_level())
		return;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	if (log_sink)
		log_sink(level, msg);
	else
		fprintf(stderr, "fpal: %s: %s\n", prefix[level], msg);
}
