// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>

#include "fpx/config.h"
#include "log.h"

static fpx_log_sink log_sink;
static bool log_debug;

void fpx_log_set_sink(fpx_log_sink sink)
{
	log_sink = sink;
}

void fpx_log_set_debug(bool on)
{
	log_debug = on;
}

static enum fpx_log_level max_level(void)
{
	const char *env = getenv(FPX_ENV_DEBUG);

	if (log_debug)
		return FPX_LOG_DEBUG;
	return (env && env[0] && env[0] != '0') ? FPX_LOG_DEBUG : FPX_LOG_INFO;
}

void fpx_log(enum fpx_log_level level, const char *fmt, ...)
{
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
		fprintf(stderr, "fpx: %s: %s\n", prefix[level], msg);
}
