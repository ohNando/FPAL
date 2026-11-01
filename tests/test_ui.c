// SPDX-License-Identifier: Apache-2.0
/*
 * Terminal looks: when colour and UTF-8 are used, and the table layout.
 */
#include <stdlib.h>
#include <string.h>

#include "../tools/ui.h"
#include "t.h"

static void test_plain_when_not_a_terminal(void) {
	struct fpal_ui ui = fpal_ui_make(false, NULL, "xterm", "en_US.UTF-8");

	T_CHECK(!ui.color);
	T_CHECK(!ui.utf8);
	T_CHECK(!ui.fancy);
	T_CHECK(!strcmp(fpal_ui_style(&ui, UI_RED), ""));
	T_CHECK(!strcmp(fpal_ui_mark(&ui, UI_OK), "ok"));
}

static void test_terminal_with_utf8(void) {
	struct fpal_ui ui = fpal_ui_make(true, NULL, "xterm-256color", "en_US.UTF-8");

	T_CHECK(ui.color && ui.utf8 && ui.fancy);
	T_CHECK(!strcmp(fpal_ui_style(&ui, UI_RESET), "\033[0m"));
	T_CHECK(!strcmp(fpal_ui_mark(&ui, UI_OK), "\xe2\x9c\x93")); /* check mark */
	T_CHECK(fpal_ui_make(true, NULL, "xterm", "tr_TR.utf8").utf8);
}

static void test_no_color_and_dumb_terminals(void) {
	struct fpal_ui ui = fpal_ui_make(true, "1", "xterm", "en_US.UTF-8");

	T_CHECK(!ui.color); /* NO_COLOR */
	T_CHECK(ui.utf8);   /* it only concerns colour */
	T_CHECK(fpal_ui_make(true, "", "xterm", "en_US.UTF-8").color); /* empty = unset */

	ui = fpal_ui_make(true, NULL, "dumb", "en_US.UTF-8");
	T_CHECK(!ui.color && !ui.utf8);
	ui = fpal_ui_make(true, NULL, "xterm", "C");
	T_CHECK(ui.color && !ui.utf8);
	ui = fpal_ui_make(true, NULL, NULL, NULL);
	T_CHECK(!ui.utf8);
}

static void test_marks_have_a_colour(void) {
	T_EQ_INT(fpal_ui_mark_style(UI_OK), UI_GREEN);
	T_EQ_INT(fpal_ui_mark_style(UI_BAD), UI_RED);
	T_EQ_INT(fpal_ui_mark_style(UI_WARN), UI_YELLOW);
	T_EQ_INT(fpal_ui_mark_style(UI_INFO), UI_DIM);
}

static void test_columns_count_code_points(void) {
	T_EQ_INT(fpal_ui_cols(""), 0);
	T_EQ_INT(fpal_ui_cols("Linux"), 5);
	T_EQ_INT(fpal_ui_cols("Sa\xc4\x9f i\xc5\x9f" "aret"), 10); /* "Sağ işaret" */
	T_EQ_INT(strlen("Sa\xc4\x9f i\xc5\x9f" "aret"), 12); /* two letters take 2 bytes */
}

/* Capture what fpal_ui_table prints. */
static char *render(const struct fpal_ui *ui, size_t ncols, const char *const *head,
		    size_t nrows, const char *const *cells) {
	char *buf = NULL;
	size_t len = 0;
	FILE *f = open_memstream(&buf, &len);

	fpal_ui_table(ui, f, ncols, head, nrows, cells);
	fclose(f);
	return buf;
}

static void test_plain_table(void) {
	static const char *const head[] = { "#", "Name" };
	static const char *const cells[] = { "0", "Windows finger", "1", "Linux" };
	struct fpal_ui ui = fpal_ui_make(false, NULL, NULL, NULL);
	char *out = render(&ui, 2, head, 2, cells);
	static const char want[] = "  #  Name\n"
				   "  -  --------------\n"
				   "  0  Windows finger\n"
				   "  1  Linux\n";

	T_CHECK(out != NULL);
	T_CHECK(out && !strcmp(out, want));
	free(out);
}

static void test_table_aligns_utf8_names(void) {
	static const char *const head[] = { "Name", "Id" };
	static const char *const cells[] = { "Sa\xc4\x9f i\xc5\x9f" "aret", "aa",
					     "Linux", "bb" };
	struct fpal_ui ui = fpal_ui_make(true, "1", "xterm", "en_US.UTF-8");
	char *out = render(&ui, 2, head, 2, cells);
	const char *second;

	T_CHECK(out != NULL);
	if (!out)
		return;
	/* the Id column starts at the same display column on every row */
	second = strstr(out, "Linux");
	T_CHECK(second != NULL);
	T_CHECK(strstr(out, "Sa\xc4\x9f i\xc5\x9f" "aret  aa") != NULL);
	T_CHECK(second && strstr(second, "Linux       bb") != NULL); /* 10 columns wide + 2 */
	T_CHECK(strstr(out, "\xe2\x94\x80") != NULL);		   /* box drawing rule */
	free(out);
}

static void test_table_ignores_bad_column_counts(void) {
	static const char *const head[] = { "a" };
	struct fpal_ui ui = fpal_ui_make(false, NULL, NULL, NULL);
	char *out = render(&ui, 0, head, 0, NULL);

	T_CHECK(out != NULL && out[0] == '\0');
	free(out);
	out = render(&ui, 99, head, 0, NULL);
	T_CHECK(out != NULL && out[0] == '\0');
	free(out);
}

int main(void) {
	T_RUN(test_plain_when_not_a_terminal);
	T_RUN(test_terminal_with_utf8);
	T_RUN(test_no_color_and_dumb_terminals);
	T_RUN(test_marks_have_a_colour);
	T_RUN(test_columns_count_code_points);
	T_RUN(test_plain_table);
	T_RUN(test_table_aligns_utf8_names);
	T_RUN(test_table_ignores_bad_column_counts);
	return T_DONE();
}
