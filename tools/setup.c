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

#define SCRIPT "platform.sh"

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

static bool trusted_stat(const struct stat *st, uid_t owner) {
	return st->st_uid == owner && !(st->st_mode & 022);
}

bool fpal_setup_script_trusted(const char *script, uid_t exe_owner) {
	char dir[PATH_MAX], *slash;
	struct stat st;

	if (stat(script, &st) < 0 || !S_ISREG(st.st_mode) ||
	    !trusted_stat(&st, exe_owner))
		return false;
	if (strlen(script) >= sizeof(dir))
		return false;
	strcpy(dir, script);
	slash = strrchr(dir, '/');
	if (!slash)
		return false;
	*(slash == dir ? slash + 1 : slash) = '\0';
	return stat(dir, &st) == 0 && S_ISDIR(st.st_mode) &&
	       trusted_stat(&st, exe_owner);
}

static const char *const backends[] = { "authselect", "pam-auth-update",
					"manual" };

bool fpal_setup_backend_valid(const char *name) {
	for (size_t i = 0; name && i < sizeof(backends) / sizeof(backends[0]); i++)
		if (!strcmp(name, backends[i]))
			return true;
	return false;
}

int fpal_setup_parse(int argc, char **argv, struct fpal_setup_args *out) {
	memset(out, 0, sizeof(*out));
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i], *name = NULL;

		if (!strcmp(a, "--revert"))
			out->revert = true;
		else if (!strcmp(a, "--dry-run"))
			out->dry_run = true;
		else if (!strcmp(a, "--no-services"))
			out->no_services = true;
		else if (!strcmp(a, "--backend") && i + 1 < argc)
			name = argv[++i];
		else if (!strncmp(a, "--backend=", 10))
			name = a + 10;
		else
			return FPAL_E_INVAL;
		if (name) {
			if (!fpal_setup_backend_valid(name))
				return FPAL_E_INVAL;
			snprintf(out->backend, sizeof(out->backend), "%s", name);
		}
	}
	return FPAL_OK;
}

static bool have_tool(const char *root, const char *dir, const char *tool) {
	char path[PATH_MAX];
	struct stat st;

	if ((size_t)snprintf(path, sizeof(path), "%s%s/%s", root, dir, tool) >=
	    sizeof(path))
		return false;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode) &&
	       (st.st_mode & 0111);
}

const char *fpal_setup_pick_backend(const char *root) {
	static const char *const dirs[] = { "/usr/bin", "/usr/sbin", "/bin",
					    "/sbin" };

	for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
		if (have_tool(root, dirs[i], "authselect"))
			return "authselect";
	for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
		if (have_tool(root, dirs[i], "pam-auth-update"))
			return "pam-auth-update";
	return "manual";
}

static void usage(void) {
	fputs("usage: fpal setup [--revert] [--dry-run] [--no-services]\n"
	      "                  [--backend authselect|pam-auth-update|manual]\n"
	      "\n"
	      "  --revert       undo what 'fpal setup' did\n"
	      "  --dry-run      only say what would be done\n"
	      "  --no-services  leave fprintd and fpald alone\n"
	      "  --backend      how to change the PAM files; by default the one\n"
	      "                 this system has ('manual' prints what to do)\n",
	      stderr);
}

int fpal_cmd_setup(int argc, char **argv) {
	static char *const clean_env[] = {
		"PATH=/usr/sbin:/usr/bin:/sbin:/bin", "LC_ALL=C", NULL
	};
	char exe[PATH_MAX], script[PATH_MAX];
	struct fpal_setup_args args;
	char *child[8];
	const char *backend;
	struct stat st;
	ssize_t n;
	char *slash;
	int k = 0;

	if (fpal_setup_parse(argc, argv, &args) != FPAL_OK) {
		usage();
		return 2;
	}
	backend = args.backend[0] ? args.backend : fpal_setup_pick_backend("");
	/* only a real change to the system needs root */
	if (geteuid() != 0 && !args.dry_run && strcmp(backend, "manual") != 0) {
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
			"fpal: refusing to run %s: it and its directory must belong "
			"to the same user as fpal and nobody else may be able to "
			"write to them\n",
			script);
		return 1;
	}

	child[k++] = script;
	child[k++] = (char *)backend;
	child[k++] = args.revert ? "revert" : "install";
	if (args.dry_run)
		child[k++] = "--dry-run";
	if (args.no_services)
		child[k++] = "--no-services";
	child[k] = NULL;

	fprintf(stderr, "fpal: setup backend: %s%s\n", backend,
		args.backend[0] ? "" : " (detected)");
	/* a clean environment: nothing the caller set reaches a root script */
	execve(script, child, clean_env);
	fprintf(stderr, "fpal: cannot run %s: %s\n", script, strerror(errno));
	return 1;
}
