// SPDX-License-Identifier: Apache-2.0
/*
 * Finding and trusting the helper script that "fpal setup" runs as root.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../tools/setup.h"
#include "fpal/types.h"
#include "t.h"

static char root[64];

static void touch(const char *path, mode_t mode) {
	FILE *f = fopen(path, "w");

	T_CHECK(f != NULL);
	if (f) {
		fputs("#!/bin/sh\n", f);
		fclose(f);
	}
	chmod(path, mode);
}

static void path_in(char *out, size_t cap, const char *rel) {
	snprintf(out, cap, "%s/%s", root, rel);
}

static void test_finds_the_installed_layout(void) {
	char dir[160], bin[160], script[160], found[PATH_MAX];

	path_in(script, sizeof(script), "share");
	(void)mkdir(script, 0755);
	path_in(dir, sizeof(dir), "share/fpal");
	(void)mkdir(dir, 0755);
	path_in(bin, sizeof(bin), "bin");
	(void)mkdir(bin, 0755);
	(void)mkdir(dir, 0755);
	path_in(script, sizeof(script), "share/fpal/platform.sh");
	touch(script, 0755);

	T_EQ_INT(fpal_setup_find_script(bin, found, sizeof(found)), FPAL_OK);
	T_CHECK(strstr(found, "share/fpal/platform.sh") != NULL);
	T_CHECK(found[0] == '/'); /* resolved, no "../" left */
	T_CHECK(strstr(found, "..") == NULL);
}

static void test_finds_a_source_checkout(void) {
	char dir[160], build[160], script[160], found[PATH_MAX];

	path_in(dir, sizeof(dir), "checkout/scripts");
	path_in(build, sizeof(build), "checkout/build");
	path_in(script, sizeof(script), "checkout");
	(void)mkdir(script, 0755);
	(void)mkdir(dir, 0755);
	(void)mkdir(build, 0755);
	path_in(script, sizeof(script), "checkout/scripts/platform.sh");
	touch(script, 0755);

	T_EQ_INT(fpal_setup_find_script(build, found, sizeof(found)), FPAL_OK);
	T_CHECK(strstr(found, "scripts/platform.sh") != NULL);
}

static void test_missing_script(void) {
	char found[PATH_MAX], empty[160];

	path_in(empty, sizeof(empty), "nothing");
	(void)mkdir(empty, 0755);
	path_in(empty, sizeof(empty), "nothing/bin");
	(void)mkdir(empty, 0755); /* a real directory with no script around it */
	T_EQ_INT(fpal_setup_find_script(empty, found, sizeof(found)),
		 FPAL_E_NODATA);
	T_EQ_INT(fpal_setup_find_script(root, found, 4), FPAL_E_NODATA); /* too small */
}

static void test_trust_rules(void) {
	char script[160];
	const uid_t me = geteuid();

	path_in(script, sizeof(script), "trust.sh");
	touch(script, 0755);
	T_CHECK(fpal_setup_script_trusted(script, me));
	T_CHECK(!fpal_setup_script_trusted(script, me + 1)); /* other owner */

	chmod(script, 0775); /* group writable */
	T_CHECK(!fpal_setup_script_trusted(script, me));
	chmod(script, 0757); /* world writable */
	T_CHECK(!fpal_setup_script_trusted(script, me));

	T_CHECK(!fpal_setup_script_trusted("/nonexistent/x.sh", me));
	T_CHECK(!fpal_setup_script_trusted(root, me)); /* a directory */
}

static void test_a_writable_directory_makes_the_script_untrusted(void) {
	char dir[160], script[200];
	const uid_t me = geteuid();

	path_in(dir, sizeof(dir), "trustdir");
	(void)mkdir(dir, 0755);
	snprintf(script, sizeof(script), "%s/run.sh", dir);
	touch(script, 0755);
	T_CHECK(fpal_setup_script_trusted(script, me));

	chmod(dir, 0775); /* others in the group could swap the file */
	T_CHECK(!fpal_setup_script_trusted(script, me));
	chmod(dir, 0757);
	T_CHECK(!fpal_setup_script_trusted(script, me));
	chmod(dir, 0755);
	T_CHECK(!fpal_setup_script_trusted(script, me + 1)); /* other owner */
}

static void test_parse(void) {
	struct fpal_setup_args a;
	char *none[] = { "setup" };
	char *all[] = { "setup", "--revert", "--dry-run", "--no-services",
			"--backend", "manual" };
	char *eq[] = { "setup", "--backend=pam-auth-update" };

	T_EQ_INT(fpal_setup_parse(1, none, &a), FPAL_OK);
	T_CHECK(!a.revert && !a.dry_run && !a.no_services && a.backend[0] == '\0');

	T_EQ_INT(fpal_setup_parse(6, all, &a), FPAL_OK);
	T_CHECK(a.revert && a.dry_run && a.no_services);
	T_CHECK(!strcmp(a.backend, "manual"));

	T_EQ_INT(fpal_setup_parse(2, eq, &a), FPAL_OK);
	T_CHECK(!strcmp(a.backend, "pam-auth-update"));
}

static void test_parse_refuses_anything_else(void) {
	struct fpal_setup_args a;
	char *unknown[] = { "setup", "--force" };
	char *positional[] = { "setup", "install" };
	char *no_value[] = { "setup", "--backend" };
	char *bad_name[] = { "setup", "--backend", "yum" };
	char *bad_eq[] = { "setup", "--backend=" };
	char *evil[] = { "setup", "--backend", "../../tmp/x" };

	T_EQ_INT(fpal_setup_parse(2, unknown, &a), FPAL_E_INVAL);
	T_EQ_INT(fpal_setup_parse(2, positional, &a), FPAL_E_INVAL);
	T_EQ_INT(fpal_setup_parse(2, no_value, &a), FPAL_E_INVAL);
	T_EQ_INT(fpal_setup_parse(3, bad_name, &a), FPAL_E_INVAL);
	T_EQ_INT(fpal_setup_parse(2, bad_eq, &a), FPAL_E_INVAL);
	T_EQ_INT(fpal_setup_parse(3, evil, &a), FPAL_E_INVAL);
	T_CHECK(fpal_setup_backend_valid("authselect"));
	T_CHECK(!fpal_setup_backend_valid(NULL));
	T_CHECK(!fpal_setup_backend_valid("Authselect"));
}

static void fake_tool(const char *rel_dir, const char *tool, mode_t mode) {
	char dir[160], path[200];

	path_in(dir, sizeof(dir), rel_dir);
	{
		char cmd[400];

		snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
		if (system(cmd) != 0)
			return;
	}
	snprintf(path, sizeof(path), "%s/%s", dir, tool);
	touch(path, mode);
}

static void test_picks_the_backend_this_system_has(void) {
	char sys[160];

	path_in(sys, sizeof(sys), "fedora");
	fake_tool("fedora/usr/bin", "authselect", 0755);
	T_CHECK(!strcmp(fpal_setup_pick_backend(sys), "authselect"));

	path_in(sys, sizeof(sys), "debian");
	fake_tool("debian/usr/sbin", "pam-auth-update", 0755);
	T_CHECK(!strcmp(fpal_setup_pick_backend(sys), "pam-auth-update"));

	/* both: authselect wins */
	path_in(sys, sizeof(sys), "both");
	fake_tool("both/usr/bin", "authselect", 0755);
	fake_tool("both/usr/sbin", "pam-auth-update", 0755);
	T_CHECK(!strcmp(fpal_setup_pick_backend(sys), "authselect"));

	/* neither, or a file that cannot run: print instructions only */
	path_in(sys, sizeof(sys), "arch");
	fake_tool("arch/usr/bin", "ls", 0755);
	T_CHECK(!strcmp(fpal_setup_pick_backend(sys), "manual"));
	path_in(sys, sizeof(sys), "broken");
	fake_tool("broken/usr/bin", "authselect", 0644);
	T_CHECK(!strcmp(fpal_setup_pick_backend(sys), "manual"));
	T_CHECK(!strcmp(fpal_setup_pick_backend("/nonexistent"), "manual"));
}

int main(void) {
	char tmpl[] = "/tmp/fpal-setup-XXXXXX";

	if (!mkdtemp(tmpl)) {
		perror("mkdtemp");
		return 1;
	}
	snprintf(root, sizeof(root), "%s", tmpl);

	T_RUN(test_finds_the_installed_layout);
	T_RUN(test_finds_a_source_checkout);
	T_RUN(test_missing_script);
	T_RUN(test_trust_rules);
	T_RUN(test_a_writable_directory_makes_the_script_untrusted);
	T_RUN(test_parse);
	T_RUN(test_parse_refuses_anything_else);
	T_RUN(test_picks_the_backend_this_system_has);

	{
		char cmd[96];

		snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpl);
		if (system(cmd) != 0)
			fputs("cleanup failed\n", stderr);
	}
	return T_DONE();
}
