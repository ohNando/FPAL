// SPDX-License-Identifier: Apache-2.0
#include "dbus/marshal.h"

#include <string.h>

#include "core/bytes.h"

/* ---- writer ------------------------------------------------------------ */

void fpal_dw_init(struct fpal_dw *w, uint8_t *buf, size_t cap) {
	w->p = buf;
	w->cap = cap;
	w->len = 0;
	w->err = 0;
}

static uint8_t *dw_room(struct fpal_dw *w, size_t n) {
	uint8_t *at;

	if (w->err)
		return NULL;
	if (n > w->cap - w->len) {
		w->err = FPAL_E_OVERFLOW;
		return NULL;
	}
	at = w->p + w->len;
	w->len += n;
	return at;
}

void fpal_dw_align(struct fpal_dw *w, size_t n) {
	size_t pad = (n - (w->len & (n - 1))) & (n - 1);
	uint8_t *at = dw_room(w, pad);

	if (at)
		memset(at, 0, pad);
}

void fpal_dw_u8(struct fpal_dw *w, uint8_t v) {
	uint8_t *at = dw_room(w, 1);

	if (at)
		*at = v;
}

void fpal_dw_u32(struct fpal_dw *w, uint32_t v) {
	uint8_t *at;

	fpal_dw_align(w, 4);
	at = dw_room(w, 4);
	if (at)
		fpal_put_le32(at, v);
}

void fpal_dw_i32(struct fpal_dw *w, int32_t v) {
	fpal_dw_u32(w, (uint32_t)v);
}

void fpal_dw_str(struct fpal_dw *w, const char *s) {
	size_t n = strlen(s);
	uint8_t *at;

	if (n > UINT32_MAX) {
		w->err = FPAL_E_OVERFLOW;
		return;
	}
	fpal_dw_u32(w, (uint32_t)n);
	at = dw_room(w, n + 1);
	if (at)
		memcpy(at, s, n + 1);
}

void fpal_dw_sig(struct fpal_dw *w, const char *s) {
	size_t n = strlen(s);
	uint8_t *at;

	if (n > 255) {
		w->err = FPAL_E_OVERFLOW;
		return;
	}
	fpal_dw_u8(w, (uint8_t)n);
	at = dw_room(w, n + 1);
	if (at)
		memcpy(at, s, n + 1);
}

void fpal_dw_variant(struct fpal_dw *w, const char *sig) {
	fpal_dw_sig(w, sig);
}

void fpal_dw_array_begin(struct fpal_dw *w, size_t elem_align,
			 struct fpal_dw_arr *arr) {
	fpal_dw_u32(w, 0); /* patched by array_end */
	arr->len_off = w->len - 4;
	/* Padding up to the first element is not part of the length. */
	fpal_dw_align(w, elem_align);
	arr->start = w->len;
}

void fpal_dw_array_end(struct fpal_dw *w, const struct fpal_dw_arr *arr) {
	if (!w->err)
		fpal_put_le32(w->p + arr->len_off,
			      (uint32_t)(w->len - arr->start));
}

/* ---- reader ------------------------------------------------------------ */

void fpal_dr_init(struct fpal_dr *r, const uint8_t *buf, size_t len) {
	r->p = buf;
	r->len = len;
	r->off = 0;
	r->err = 0;
}

static const uint8_t *dr_take(struct fpal_dr *r, size_t n) {
	const uint8_t *at;

	if (r->err)
		return NULL;
	if (n > r->len - r->off) {
		r->err = FPAL_E_PROTO;
		return NULL;
	}
	at = r->p + r->off;
	r->off += n;
	return at;
}

void fpal_dr_align(struct fpal_dr *r, size_t n) {
	size_t pad = (n - (r->off & (n - 1))) & (n - 1);
	const uint8_t *at = dr_take(r, pad);

	/* Padding bytes must be zero. */
	for (size_t i = 0; at && i < pad; i++) {
		if (at[i] != 0)
			r->err = FPAL_E_PROTO;
	}
}

uint8_t fpal_dr_u8(struct fpal_dr *r) {
	const uint8_t *at = dr_take(r, 1);

	return at ? *at : 0;
}

uint32_t fpal_dr_u32(struct fpal_dr *r) {
	const uint8_t *at;

	fpal_dr_align(r, 4);
	at = dr_take(r, 4);
	return at ? fpal_get_le32(at) : 0;
}

static const char *dr_string(struct fpal_dr *r, size_t n) {
	const uint8_t *at = dr_take(r, n + 1);

	if (!at)
		return NULL;
	if (at[n] != 0 || memchr(at, 0, n) != NULL) {
		r->err = FPAL_E_PROTO;
		return NULL;
	}
	return (const char *)at;
}

const char *fpal_dr_str(struct fpal_dr *r) {
	uint32_t n = fpal_dr_u32(r);

	return r->err ? NULL : dr_string(r, n);
}

const char *fpal_dr_sig(struct fpal_dr *r) {
	uint8_t n = fpal_dr_u8(r);

	return r->err ? NULL : dr_string(r, n);
}

uint32_t fpal_dr_array_begin(struct fpal_dr *r, size_t elem_align) {
	uint32_t n = fpal_dr_u32(r);

	fpal_dr_align(r, elem_align);
	if (!r->err && n > r->len - r->off)
		r->err = FPAL_E_PROTO;
	return r->err ? 0 : n;
}
