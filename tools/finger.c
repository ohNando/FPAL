// SPDX-License-Identifier: Apache-2.0
#include "finger.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "fpal/labels.h"

static bool all_digits(const char *s) {
	if (!*s)
		return false;
	for (; *s; s++)
		if (*s < '0' || *s > '9')
			return false;
	return true;
}

/* Does @arg start the lower-case hex form of @id? */
static bool hex_prefix_of(const struct fpal_template_id *id, const char *arg) {
	const size_t len = strlen(arg);

	if (len < 8 || len > FPAL_TEMPLATE_ID_LEN * 2)
		return false;
	for (size_t i = 0; i < len; i++) {
		const uint8_t byte = id->bytes[i / 2];
		const char want = "0123456789abcdef"[i % 2 ? byte & 15 : byte >> 4];

		if (arg[i] != want)
			return false;
	}
	return true;
}

/* Does @arg start the printable text of @id (e.g. "FPAL-7b51")? */
static bool text_prefix_of(const struct fpal_template_id *id, const char *arg) {
	const size_t len = strlen(arg);

	return len >= 6 && len <= FPAL_TEMPLATE_ID_LEN &&
	       !memcmp(id->bytes, arg, len);
}

int fpal_finger_find(const char *state_dir, const struct fpal_template_id *ids,
		     size_t n, const char *arg, size_t *index, size_t *matches) {
	char label[FPAL_LABEL_MAX + 1];
	size_t found = 0, which = 0;

	if (!ids || !arg || !index || !matches)
		return FPAL_E_INVAL;
	*matches = 0;

	if (all_digits(arg) && strlen(arg) <= 2) {
		const unsigned long v = strtoul(arg, NULL, 10);

		if (v >= n)
			return FPAL_E_NODATA;
		*index = v;
		*matches = 1;
		return FPAL_OK;
	}

	for (size_t i = 0; i < n; i++) {
		if (!fpal_labels_get(state_dir, &ids[i], label, sizeof(label)) &&
		    label[0] && !strcmp(label, arg)) {
			found++;
			which = i;
		}
	}
	if (!found) {
		for (size_t i = 0; i < n; i++) {
			if (hex_prefix_of(&ids[i], arg) ||
			    text_prefix_of(&ids[i], arg)) {
				found++;
				which = i;
			}
		}
	}
	*matches = found;
	if (found == 1) {
		*index = which;
		return FPAL_OK;
	}
	return found ? FPAL_E_EXISTS : FPAL_E_NODATA;
}
