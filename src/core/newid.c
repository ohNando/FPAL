// SPDX-License-Identifier: Apache-2.0
/*
 * Ids for new templates. Only meaningful when enrolling is built in.
 */
#include <string.h>

#include "core/driver.h"

#ifdef FPAL_ENABLE_ENROLL
#include <sys/random.h>

int fpal_template_id_make(size_t len, struct fpal_template_id *out) {
	static const char hex[] = "0123456789abcdef";
	const size_t prefix = sizeof(FPAL_NEW_ID_PREFIX) - 1;
	uint8_t rnd[FPAL_ID_MAX], raw[FPAL_ID_MAX];
	size_t n;

	if (!out)
		return FPAL_E_INVAL;
	/* Room for the prefix and at least 16 random characters. */
	if (len < prefix + 16 || len > FPAL_ID_MAX)
		return FPAL_E_INVAL;
	n = len - prefix;
	/* Every byte is printable, so the id reads well in logs. */
	if (getrandom(rnd, n, 0) != (ssize_t)n)
		return FPAL_E_IO;
	memcpy(raw, FPAL_NEW_ID_PREFIX, prefix);
	for (size_t i = 0; i < n; i++)
		raw[prefix + i] = (uint8_t)hex[rnd[i] & 0x0f];
	return fpal_id_set(out, raw, len);
}

int fpal_template_id_new(const struct fpal_device *dev,
			 struct fpal_template_id *out) {
	if (!dev)
		return FPAL_E_INVAL;
	return fpal_template_id_make(dev->drv->new_id_len, out);
}
#else
int fpal_template_id_make(size_t len, struct fpal_template_id *out) {
	(void)len;
	(void)out;
	return FPAL_E_NOTSUP;
}

int fpal_template_id_new(const struct fpal_device *dev,
			 struct fpal_template_id *out) {
	(void)dev;
	(void)out;
	return FPAL_E_NOTSUP;
}
#endif
