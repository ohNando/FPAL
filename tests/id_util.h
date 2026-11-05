/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Building template ids in tests. Tests never touch the bytes of an id
 * directly; they go through these helpers and the library's own.
 */
#ifndef FPAL_TESTS_ID_UTIL_H
#define FPAL_TESTS_ID_UTIL_H

#include <string.h>

#include "fpal/id.h"

/* The length of an Egis id; most tests use ids of this size. */
#define TEST_ID_LEN 32u

/* @len bytes: @seed, @seed + 1, ... */
static inline void id_seq(struct fpal_template_id *id, uint8_t seed, size_t len) {
	uint8_t raw[FPAL_ID_HEX_MAX / 2];

	for (size_t i = 0; i < len; i++)
		raw[i] = (uint8_t)(seed + i);
	(void)fpal_id_set(id, raw, len);
}

/* @len bytes, all equal to @byte. */
static inline void id_fill(struct fpal_template_id *id, int byte, size_t len) {
	uint8_t raw[FPAL_ID_HEX_MAX / 2];

	memset(raw, byte, len);
	(void)fpal_id_set(id, raw, len);
}

/* @text followed by zero bytes, @len bytes in all (@text must fit). */
static inline void id_pad(struct fpal_template_id *id, const char *text, size_t len) {
	uint8_t raw[FPAL_ID_HEX_MAX / 2] = { 0 };

	memcpy(raw, text, strlen(text) < len ? strlen(text) : len);
	(void)fpal_id_set(id, raw, len);
}

#endif /* FPAL_TESTS_ID_UTIL_H */
