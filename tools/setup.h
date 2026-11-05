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
 * the same user as the fpal binary (root after an install). The directory it
 * sits in has to meet the same rule: otherwise the file could be replaced.
 */
bool fpal_setup_script_trusted(const char *script, uid_t exe_owner);

#define FPAL_SETUP_BACKEND_MAX 24u

struct fpal_setup_args {
	bool revert, dry_run, no_services;
	char backend[FPAL_SETUP_BACKEND_MAX]; /* "" = pick one */
};

/* Is @name a backend of platform.sh? */
bool fpal_setup_backend_valid(const char *name);

/*
 * Parse "setup [--revert] [--dry-run] [--no-services] [--backend NAME]"
 * (argv[0] is "setup"). FPAL_E_INVAL for anything else.
 */
int fpal_setup_parse(int argc, char **argv, struct fpal_setup_args *out);

/*
 * The backend for this system: "authselect" if that tool is installed,
 * else "pam-auth-update", else "manual", which only prints instructions.
 * The tools are looked for in fixed directories below @root ("" for the real
 * system); the environment is not consulted.
 */
const char *fpal_setup_pick_backend(const char *root);

int fpal_cmd_setup(int argc, char **argv);

#endif /* FPAL_TOOLS_SETUP_H */
