/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Picking one fingerprint from the sensor's list by what a person types:
 * its number, its name, or the start of its id. Anything that deletes must
 * never guess, so a match has to be unique.
 */
#ifndef FPAL_TOOLS_FINGER_H
#define FPAL_TOOLS_FINGER_H

#include <stddef.h>

#include "fpal/types.h"

/*
 * Resolution order: a one or two digit number is an index into @ids; else an
 * exact name from the labels in @state_dir; else the start (at least 8 hex
 * digits, or 6 characters of text such as "FPAL-7") of an id.
 *
 * Returns FPAL_OK with @index set, FPAL_E_NODATA if nothing matches (a number
 * outside the list counts) and FPAL_E_EXISTS if several do. @matches gets the
 * number of candidates.
 */
int fpal_finger_find(const char *state_dir, const struct fpal_template_id *ids,
		     size_t n, const char *arg, size_t *index, size_t *matches);

#endif /* FPAL_TOOLS_FINGER_H */
