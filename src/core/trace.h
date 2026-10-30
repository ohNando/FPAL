/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Packet tracing: with FPX_TRACE=1 every USB transfer is hex dumped to
 * stderr. Used to learn the protocol and to verify response offsets.
 */
#ifndef FPX_CORE_TRACE_H
#define FPX_CORE_TRACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool fpx_trace_enabled(void);

/* @dir is a short tag such as "OUT", "IN", "CTRL", "IRQ". */
void fpx_trace(const char *dir, uint8_t ep, const uint8_t *buf, size_t len);

#endif /* FPX_CORE_TRACE_H */
