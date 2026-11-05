/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Template ids. The sensor hands out an opaque id for every fingerprint it
 * stores; its length differs between sensor families. All code outside a
 * driver goes through these helpers and never touches the bytes of an id
 * directly, so a stray fixed-length compare cannot happen.
 */
#ifndef FPAL_ID_H
#define FPAL_ID_H

#include <stdbool.h>
#include <stddef.h>

#include "types.h"

/* Hex digits of the longest id: the size of a line in the bindings and labels files. */
#define FPAL_ID_HEX_MAX (FPAL_ID_MAX * 2u)

/* Number of bytes of @id (never more than FPAL_ID_MAX) and a pointer to them. */
size_t fpal_id_len(const struct fpal_template_id *id);
const uint8_t *fpal_id_data(const struct fpal_template_id *id);

/* Store @len bytes. FPAL_E_INVAL if @len is zero or too long for an id. */
int fpal_id_set(struct fpal_template_id *id, const void *data, size_t len);

/* Same length and same bytes. */
bool fpal_id_equal(const struct fpal_template_id *a,
		   const struct fpal_template_id *b);

/* Does the id start with the ASCII text @prefix? */
bool fpal_id_has_prefix(const struct fpal_template_id *id, const char *prefix);

/* Lower case hex plus a NUL; FPAL_E_OVERFLOW if @cap is smaller than 2 * len + 1. */
int fpal_id_to_hex(const struct fpal_template_id *id, char *out, size_t cap);

/*
 * Parse @hexlen hex digits (an even number, not a NUL terminated string).
 * Upper case digits are accepted only with @allow_upper. FPAL_E_INVAL for a
 * bad digit, an odd count, an empty string or an id that would be too long.
 */
int fpal_id_from_hex(const char *hex, size_t hexlen, bool allow_upper,
		     struct fpal_template_id *id);

/* The id as text for a person: bytes outside 0x20..0x7e shown as '.', NUL terminated. */
void fpal_id_text(const struct fpal_template_id *id, char *out, size_t cap);

#endif /* FPAL_ID_H */
