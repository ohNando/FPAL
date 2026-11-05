// SPDX-License-Identifier: Apache-2.0
/*
 * Ids for new templates. Only meaningful when enrolling is built in.
 */
#include <string.h>

#include "fpal/fpal.h"

#ifdef FPAL_ENABLE_ENROLL
#include <sys/random.h>

/* Length of the ids made here: the Egis length, until drivers pick their own. */
#define NEW_ID_LEN 32u

int fpal_template_id_generate(struct fpal_template_id *out) {
	static const char hex[] = "0123456789abcdef";
	const size_t prefix = sizeof(FPAL_NEW_ID_PREFIX) - 1;
	uint8_t rnd[NEW_ID_LEN], raw[NEW_ID_LEN];
	size_t n = NEW_ID_LEN - prefix;

	if (!out)
		return FPAL_E_INVAL;
	/* All 32 bytes are printable, so the id reads well in logs. */
	if (getrandom(rnd, n, 0) != (ssize_t)n)
		return FPAL_E_IO;
	memcpy(raw, FPAL_NEW_ID_PREFIX, prefix);
	for (size_t i = 0; i < n; i++)
		raw[prefix + i] = (uint8_t)hex[rnd[i] & 0x0f];
	return fpal_id_set(out, raw, NEW_ID_LEN);
}
#else
int fpal_template_id_generate(struct fpal_template_id *out) {
	(void)out;
	return FPAL_E_NOTSUP;
}
#endif
