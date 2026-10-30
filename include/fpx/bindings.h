/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Host side user <-> template bindings.
 *
 * Templates live on the sensor; the host only remembers which template ids
 * belong to which local user. Each user has one file <dir>/<user> (root
 * owned, mode 0600) with one hex encoded template id per line.
 *
 * Unbinding only edits this file and never touches the sensor.
 */
#ifndef FPX_BINDINGS_H
#define FPX_BINDINGS_H

#include "types.h"

/* True if @user is a safe file name (portable POSIX user name subset). */
bool fpx_bindings_valid_user(const char *user);

/* Load the ids bound to @user. A missing file yields @n = 0. */
int fpx_bindings_load(const char *dir, const char *user,
		      struct fpx_template_id *out, size_t cap, size_t *n);

/* Bind @id to @user. Binding an id twice is not an error. */
int fpx_bindings_add(const char *dir, const char *user,
		     const struct fpx_template_id *id);

/* Remove every binding of @user (host side only). */
int fpx_bindings_clear(const char *dir, const char *user);

/* True if @id is bound to @user. */
bool fpx_bindings_contains(const char *dir, const char *user,
			   const struct fpx_template_id *id);

#endif /* FPX_BINDINGS_H */
