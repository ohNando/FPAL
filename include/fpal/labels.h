/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Names for templates ("Windows finger", "Linux finger").
 *
 * The sensor stores only an opaque 32 byte id per template, so the names live
 * on the host in <dir>/.labels (root owned, mode 0600, one "hex id TAB name"
 * line each). A name is therefore visible to this installation only; it is
 * never written to the sensor.
 */
#ifndef FPAL_LABELS_H
#define FPAL_LABELS_H

#include "id.h"
#include "types.h"

/*
 * A usable name: 1..FPAL_LABEL_MAX bytes of well formed UTF-8 with no leading
 * or trailing space and none of the characters that would let a name play
 * tricks on a terminal (see fpal_label_char_ok).
 */
bool fpal_label_valid(const char *label);

/*
 * May this Unicode code point appear in a name? No control characters (C0,
 * DEL and C1), no surrogates, nothing above U+10FFFF, and none of the
 * invisible or direction changing characters (zero width marks, line and
 * paragraph separators, bidirectional overrides, BOM).
 */
bool fpal_label_char_ok(uint32_t cp);

/* Copy the name of @id into @out (empty string if it has none). */
int fpal_labels_get(const char *dir, const struct fpal_template_id *id,
		    char *out, size_t cap);

/* Name @id, replacing any earlier name; NULL or "" removes the name. */
int fpal_labels_set(const char *dir, const struct fpal_template_id *id,
		    const char *label);

/* The id whose name is exactly @label. FPAL_E_NODATA if none. */
int fpal_labels_find(const char *dir, const char *label,
		     struct fpal_template_id *id);

#endif /* FPAL_LABELS_H */
