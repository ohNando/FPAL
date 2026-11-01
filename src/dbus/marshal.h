/* SPDX-License-Identifier: Apache-2.0 */
/*
 * D-Bus wire format: little-endian writer and reader with the alignment
 * rules of the specification (each value is aligned to its own size, 8 for
 * structs and dict entries). Alignment is relative to the start of the
 * buffer, so a message body (which starts on an 8 byte boundary) can be
 * built in a buffer of its own.
 *
 * Only the types fpald needs are supported: y u s o g, v, and arrays of
 * those. Errors are sticky: after the first failure every call is a no-op
 * and the final status is read from the err field.
 */
#ifndef FPAL_DBUS_MARSHAL_H
#define FPAL_DBUS_MARSHAL_H

#include <stddef.h>
#include <stdint.h>

#include "fpal/types.h"

struct fpal_dw {
	uint8_t *p;
	size_t cap;
	size_t len;
	int err; /* 0 or FPAL_E_OVERFLOW */
};

void fpal_dw_init(struct fpal_dw *w, uint8_t *buf, size_t cap);
void fpal_dw_align(struct fpal_dw *w, size_t n);
void fpal_dw_u8(struct fpal_dw *w, uint8_t v);
void fpal_dw_u32(struct fpal_dw *w, uint32_t v);
void fpal_dw_i32(struct fpal_dw *w, int32_t v);
/* STRING and OBJECT_PATH: u32 length, bytes, NUL. */
void fpal_dw_str(struct fpal_dw *w, const char *s);
/* SIGNATURE: u8 length, bytes, NUL. */
void fpal_dw_sig(struct fpal_dw *w, const char *s);
/* Variant header: writes the signature of the value that follows. */
void fpal_dw_variant(struct fpal_dw *w, const char *sig);
/*
 * Arrays: the length prefix is patched in by fpal_dw_array_end(). @elem_align
 * is the alignment of one element (8 for structs and dict entries).
 */
struct fpal_dw_arr {
	size_t len_off; /* where the u32 length lives */
	size_t start;	/* first element, after alignment padding */
};

void fpal_dw_array_begin(struct fpal_dw *w, size_t elem_align,
			 struct fpal_dw_arr *arr);
void fpal_dw_array_end(struct fpal_dw *w, const struct fpal_dw_arr *arr);

struct fpal_dr {
	const uint8_t *p;
	size_t len;
	size_t off;
	int err; /* 0 or FPAL_E_PROTO */
};

void fpal_dr_init(struct fpal_dr *r, const uint8_t *buf, size_t len);
void fpal_dr_align(struct fpal_dr *r, size_t n);
uint8_t fpal_dr_u8(struct fpal_dr *r);
uint32_t fpal_dr_u32(struct fpal_dr *r);
/*
 * Returns a pointer into the buffer, validated to be NUL terminated with no
 * embedded NUL. NULL (and err set) on malformed input.
 */
const char *fpal_dr_str(struct fpal_dr *r);
const char *fpal_dr_sig(struct fpal_dr *r);
/* Array length prefix; fails if the array would run past the buffer. */
uint32_t fpal_dr_array_begin(struct fpal_dr *r, size_t elem_align);

#endif /* FPAL_DBUS_MARSHAL_H */
