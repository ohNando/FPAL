/* SPDX-License-Identifier: Apache-2.0 */
/*
 * "fpal setup": switch the system so sudo, su and the GNOME screens use the
 * fingerprint, or back. The work is done by platform.sh, which is
 * installed next to fpal; this command finds it and runs it.
 */
#ifndef FPAL_TOOLS_SETUP_H
#define FPAL_TOOLS_SETUP_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/*
 * Where platform.sh lives for an fpal found at @exe_dir: the install
 * layout (<prefix>/bin/fpal -> <prefix>/share/fpal/) first, then a source
 * checkout (build/fpal -> scripts/). Copies the path into @out.
 */
int fpal_setup_find_script(const char *exe_dir, char *out, size_t cap);

/*
 * Only run a script that nobody but its owner can change and that belongs to
 * the same user as the fpal binary (root after an install).
 */
bool fpal_setup_script_trusted(const char *script, uid_t exe_owner);

int fpal_cmd_setup(int argc, char **argv);

#endif /* FPAL_TOOLS_SETUP_H */
