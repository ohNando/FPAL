// SPDX-License-Identifier: Apache-2.0
#include "fpal/id.h"

#include <string.h>

size_t fpal_id_len(const struct fpal_template_id *id) {
	/* a struct that was never filled in (or was overwritten) cannot make us read past it */
	return id->len > FPAL_ID_MAX ? FPAL_ID_MAX : id->len;
}

const uint8_t *fpal_id_data(const struct fpal_template_id *id) {
	return id->data;
}

int fpal_id_set(struct fpal_template_id *id, const void *data, size_t len) {
	if (!id || !data || len == 0 || len > FPAL_ID_MAX)
		return FPAL_E_INVAL;
	memcpy(id->data, data, len);
	id->len = (uint8_t)len;
	return FPAL_OK;
}

bool fpal_id_equal(const struct fpal_template_id *a,
		   const struct fpal_template_id *b) {
	return fpal_id_len(a) == fpal_id_len(b) &&
	       memcmp(fpal_id_data(a), fpal_id_data(b), fpal_id_len(a)) == 0;
}

bool fpal_id_has_prefix(const struct fpal_template_id *id, const char *prefix) {
	const size_t n = strlen(prefix);

	return n <= fpal_id_len(id) && memcmp(fpal_id_data(id), prefix, n) == 0;
}

int fpal_id_to_hex(const struct fpal_template_id *id, char *out, size_t cap) {
	static const char digits[] = "0123456789abcdef";
	const size_t n = fpal_id_len(id);
	const uint8_t *d = fpal_id_data(id);

	if (cap < 2 * n + 1)
		return FPAL_E_OVERFLOW;
	for (size_t i = 0; i < n; i++) {
		out[2 * i] = digits[d[i] >> 4];
		out[2 * i + 1] = digits[d[i] & 0x0f];
	}
	out[2 * n] = '\0';
	return FPAL_OK;
}

static int nibble(char c, bool allow_upper) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (allow_upper && c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int fpal_id_from_hex(const char *hex, size_t hexlen, bool allow_upper,
		     struct fpal_template_id *id) {
	uint8_t raw[FPAL_ID_MAX];
	const size_t n = hexlen / 2;

	if (!hex || !id || hexlen == 0 || hexlen % 2 != 0 || n > FPAL_ID_MAX)
		return FPAL_E_INVAL;
	for (size_t i = 0; i < n; i++) {
		const int hi = nibble(hex[2 * i], allow_upper);
		const int lo = nibble(hex[2 * i + 1], allow_upper);

		if (hi < 0 || lo < 0)
			return FPAL_E_INVAL;
		raw[i] = (uint8_t)(hi << 4 | lo);
	}
	return fpal_id_set(id, raw, n);
}

void fpal_id_text(const struct fpal_template_id *id, char *out, size_t cap) {
	const size_t n = fpal_id_len(id);
	const uint8_t *d = fpal_id_data(id);
	size_t i;

	if (cap == 0)
		return;
	for (i = 0; i < n && i + 1 < cap; i++)
		out[i] = (d[i] >= 0x20 && d[i] <= 0x7e) ? (char)d[i] : '.';
	out[i] = '\0';
}
