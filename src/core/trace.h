/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Packet tracing: with FPAL_TRACE=1 every USB transfer is hex dumped to
 * stderr. Used to learn the protocol and to verify response offsets.
 */
#ifndef FPAL_CORE_TRACE_H
#define FPAL_CORE_TRACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool fpal_trace_enabled(void);

/*
 * Decide tracing in code instead of FPAL_TRACE: 1 on, 0 off, -1 (the
 * default) read the environment. pam_fpal switches it off for good.
 */
void fpal_trace_set(int mode);

/* @dir is a short tag such as "OUT", "IN", "CTRL", "IRQ". */
void fpal_trace(const char *dir, uint8_t ep, const uint8_t *buf, size_t len);

#endif /* FPAL_CORE_TRACE_H */
