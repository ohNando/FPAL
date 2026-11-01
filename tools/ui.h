/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Terminal looks for the command line tool: colours, check marks and
 * tables. Everything degrades to plain ASCII when the output is not a
 * terminal, NO_COLOR is set or the terminal is "dumb", so pipes and logs
 * stay readable.
 */
#ifndef FPAL_TOOLS_UI_H
#define FPAL_TOOLS_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

enum fpal_style { UI_RESET, UI_BOLD, UI_DIM, UI_RED, UI_GREEN, UI_YELLOW };

enum fpal_mark { UI_OK, UI_BAD, UI_WARN, UI_INFO };

struct fpal_ui {
	bool color; /* escape sequences */
	bool utf8;  /* ✓ and box drawing instead of ASCII */
	bool fancy; /* output is a terminal: tables, not the raw format */
};

/* The decision from its inputs (pure, for tests). */
struct fpal_ui fpal_ui_make(bool tty, const char *no_color, const char *term,
			    const char *lang);

/* The decision for @f from the environment. */
struct fpal_ui fpal_ui_init(FILE *f);

/* Escape sequence for @s, or "" when colour is off. */
const char *fpal_ui_style(const struct fpal_ui *ui, enum fpal_style s);

/* Glyph for @m: ✓ ✗ ! · or ok !! ?? -- (no colour). */
const char *fpal_ui_mark(const struct fpal_ui *ui, enum fpal_mark m);

/* The colour that goes with @m. */
enum fpal_style fpal_ui_mark_style(enum fpal_mark m);

/* Display columns of a UTF-8 string (one per code point). */
size_t fpal_ui_cols(const char *s);

/* Print @s padded with spaces to @width columns. */
void fpal_ui_pad(FILE *f, const char *s, size_t width);

/*
 * Print a table: @ncols columns, a header row and @nrows rows. @cells holds
 * nrows * ncols strings row by row. Columns are as wide as their widest cell.
 */
void fpal_ui_table(const struct fpal_ui *ui, FILE *f, size_t ncols,
		   const char *const *head, size_t nrows,
		   const char *const *cells);

/* The user a command is run for: SUDO_USER if set, else the caller. */
const char *fpal_ui_invoking_user(void);

#endif /* FPAL_TOOLS_UI_H */
