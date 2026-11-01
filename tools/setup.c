// SPDX-License-Identifier: Apache-2.0
#include "setup.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fpal/types.h"

#define SCRIPT "authselect-fpal.sh"

int fpal_setup_find_script(const char *exe_dir, char *out, size_t cap) {
	static const char *const layouts[] = {
		"%s/../share/fpal/" SCRIPT, /* installed: <prefix>/bin/fpal */
		"%s/../scripts/" SCRIPT,    /* checkout:  build/fpal */
	};

	for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
		char path[PATH_MAX], real[PATH_MAX];

		if ((size_t)snprintf(path, sizeof(path), layouts[i], exe_dir) >=
		    sizeof(path))
			continue;
		if (!realpath(path, real) || strlen(real) >= cap)
			continue;
		strcpy(out, real);
		return FPAL_OK;
	}
	return FPAL_E_NODATA;
}

bool fpal_setup_script_trusted(const char *script, uid_t exe_owner) {
	struct stat st;

	if (stat(script, &st) < 0)
		return false;
	return S_ISREG(st.st_mode) && st.st_uid == exe_owner &&
	       !(st.st_mode & 022);
}

int fpal_cmd_setup(int argc, char **argv) {
	char exe[PATH_MAX], script[PATH_MAX];
	const char *mode = "install";
	struct stat st;
	ssize_t n;
	char *slash;

	if (argc > 2 || (argc == 2 && strcmp(argv[1], "--revert") != 0)) {
		fputs("usage: fpal setup [--revert]\n", stderr);
		return 2;
	}
	if (argc == 2)
		mode = "revert";
	if (geteuid() != 0) {
		fputs("fpal: setup changes the system's login configuration; "
		      "run it as root (sudo fpal setup)\n",
		      stderr);
		return 1;
	}

	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n <= 0 || stat("/proc/self/exe", &st) < 0) {
		fputs("fpal: cannot tell where fpal is installed\n", stderr);
		return 1;
	}
	exe[n] = '\0';
	slash = strrchr(exe, '/');
	if (slash)
		*slash = '\0';

	if (fpal_setup_find_script(exe, script, sizeof(script))) {
		fputs("fpal: cannot find " SCRIPT " next to fpal; is the "
		      "package installed?\n",
		      stderr);
		return 1;
	}
	if (!fpal_setup_script_trusted(script, st.st_uid)) {
		fprintf(stderr,
			"fpal: refusing to run %s: it must belong to the same user "
			"as fpal and nobody else may be able to write to it\n",
			script);
		return 1;
	}

	execl(script, script, mode, (char *)NULL);
	fprintf(stderr, "fpal: cannot run %s: %s\n", script, strerror(errno));
	return 1;
}
