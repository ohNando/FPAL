/* SPDX-License-Identifier: Apache-2.0 */
/*
 * "fpal status": one screen that says whether fingerprint login is ready and,
 * if not, what to do about it.
 */
#ifndef FPAL_TOOLS_STATUS_H
#define FPAL_TOOLS_STATUS_H

#include <stdbool.h>

/* Does @path contain a PAM line (not a comment) that uses @module? */
bool fpal_file_has_module(const char *path, const char *module);

int fpal_cmd_status(int argc, char **argv);

#endif /* FPAL_TOOLS_STATUS_H */
