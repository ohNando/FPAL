/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host side user <-> template bindings.
 *
 * Templates live on the sensor; the host only remembers which template ids
 * belong to which local user. Each user has one file <dir>/<user> (root
 * owned, mode 0600) with one hex encoded template id per line.
 *
 * Unbinding only edits this file and never touches the sensor.
 */
#ifndef FPAL_BINDINGS_H
#define FPAL_BINDINGS_H

#include "id.h"
#include "types.h"

/* True if @user is a safe file name (portable POSIX user name subset). */
bool fpal_bindings_valid_user(const char *user);

/* Load the ids bound to @user. A missing file yields @n = 0. */
int fpal_bindings_load(const char *dir, const char *user,
		      struct fpal_template_id *out, size_t cap, size_t *n);

/* Bind @id to @user. Binding an id twice is not an error. */
int fpal_bindings_add(const char *dir, const char *user,
		     const struct fpal_template_id *id);

/* Remove every binding of @user (host side only). */
int fpal_bindings_clear(const char *dir, const char *user);

/* Drop @id from @user's bindings (host side only; no error if absent). */
int fpal_bindings_remove(const char *dir, const char *user,
			 const struct fpal_template_id *id);

/* Drop @id from the bindings of every user, e.g. after it was deleted. */
int fpal_bindings_forget(const char *dir, const struct fpal_template_id *id);

/* True if @id is bound to @user. */
bool fpal_bindings_contains(const char *dir, const char *user,
			   const struct fpal_template_id *id);

#endif /* FPAL_BINDINGS_H */
