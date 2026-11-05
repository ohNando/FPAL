// SPDX-License-Identifier: Apache-2.0
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "fpal/config.h"
#include "trace.h"

static int trace_mode = -1;

void fpal_trace_set(int mode) {
	trace_mode = mode;
}

bool fpal_trace_enabled(void) {
	const char *env;

	if (trace_mode >= 0)
		return trace_mode > 0;
	env = getenv(FPAL_ENV_TRACE);
	return env && env[0] && env[0] != '0';
}

/* Milliseconds since the first traced transfer: shows where the time goes. */
static double elapsed_ms(void) {
	static struct timespec first;
	static bool started;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (!started) {
		first = now;
		started = true;
	}
	return (double)(now.tv_sec - first.tv_sec) * 1000.0 +
	       (double)(now.tv_nsec - first.tv_nsec) / 1e6;
}

void fpal_trace(const char *dir, uint8_t ep, const uint8_t *buf, size_t len) {
	if (!fpal_trace_enabled())
		return;

	fprintf(stderr, "[trace +%9.3f ms] %-4s ep=0x%02x len=%zu\n", elapsed_ms(),
		dir, ep, len);

	for (size_t off = 0; off < len; off += 16) {
		fprintf(stderr, "  %04zx  ", off);
		for (size_t i = 0; i < 16; i++) {
			if (off + i < len)
				fprintf(stderr, "%02x ", buf[off + i]);
			else
				fputs("   ", stderr);
		}
		fputs(" |", stderr);
		for (size_t i = 0; i < 16 && off + i < len; i++)
			fputc(isprint(buf[off + i]) ? buf[off + i] : '.',
			      stderr);
		fputs("|\n", stderr);
	}
}
