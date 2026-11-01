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
	path_in(script, sizeof(script), "share/fpal/authselect-fpal.sh");
	touch(script, 0755);

	T_EQ_INT(fpal_setup_find_script(bin, found, sizeof(found)), FPAL_OK);
	T_CHECK(strstr(found, "share/fpal/authselect-fpal.sh") != NULL);
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
	path_in(script, sizeof(script), "checkout/scripts/authselect-fpal.sh");
	touch(script, 0755);

	T_EQ_INT(fpal_setup_find_script(build, found, sizeof(found)), FPAL_OK);
	T_CHECK(strstr(found, "scripts/authselect-fpal.sh") != NULL);
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

	{
		char cmd[96];

		snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpl);
		if (system(cmd) != 0)
			fputs("cleanup failed\n", stderr);
	}
	return T_DONE();
}
