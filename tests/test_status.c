// SPDX-License-Identifier: Apache-2.0
/*
 * Reading PAM files for "fpal status".
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../tools/status.h"
#include "t.h"

static char path[96];

static void write_file(const char *text) {
	FILE *f = fopen(path, "w");

	T_CHECK(f != NULL);
	if (f) {
		fputs(text, f);
		fclose(f);
	}
}

static void test_active_line(void) {
	write_file("auth required pam_env.so\n"
		   "auth [success=done default=bad] pam_fpal.so\n"
		   "auth sufficient pam_unix.so\n");
	T_CHECK(fpal_file_has_module(path, "pam_fpal.so"));
	T_CHECK(!fpal_file_has_module(path, "pam_fprintd.so"));
}

static void test_comments_do_not_count(void) {
	write_file("# auth sufficient pam_fpal.so\n"
		   "   \t# auth sufficient pam_fpal.so\n"
		   "auth sufficient pam_unix.so\n");
	T_CHECK(!fpal_file_has_module(path, "pam_fpal.so"));
}

static void test_indented_line_and_missing_file(void) {
	write_file("  \tauth sufficient pam_fpal.so quiet\n");
	T_CHECK(fpal_file_has_module(path, "pam_fpal.so"));
	T_CHECK(!fpal_file_has_module("/nonexistent/pam.d/x", "pam_fpal.so"));
}

int main(void) {
	char tmpl[] = "/tmp/fpal-status-XXXXXX";
	int fd = mkstemp(tmpl);

	if (fd < 0) {
		perror("mkstemp");
		return 1;
	}
	close(fd);
	snprintf(path, sizeof(path), "%s", tmpl);

	T_RUN(test_active_line);
	T_RUN(test_comments_do_not_count);
	T_RUN(test_indented_line_and_missing_file);

	unlink(tmpl);
	return T_DONE();
}
