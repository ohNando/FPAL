// SPDX-License-Identifier: Apache-2.0
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "fpx/config.h"
#include "trace.h"

bool fpx_trace_enabled(void) {
	const char *env = getenv(FPX_ENV_TRACE);

	return env && env[0] && env[0] != '0';
}

void fpx_trace(const char *dir, uint8_t ep, const uint8_t *buf, size_t len) {
	if (!fpx_trace_enabled())
		return;

	fprintf(stderr, "[trace] %-4s ep=0x%02x len=%zu\n", dir, ep, len);

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
