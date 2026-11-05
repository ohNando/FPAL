// SPDX-License-Identifier: Apache-2.0
#include "ui.h"

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COLS_MAX 8u

struct fpal_ui fpal_ui_make(bool tty, const char *no_color, const char *term,
			    const char *lang) {
	struct fpal_ui ui = { .color = false, .utf8 = false, .fancy = tty };
	const bool dumb = term && !strcmp(term, "dumb");

	/* https://no-color.org: any non-empty value turns colour off */
	ui.color = tty && !dumb && !(no_color && *no_color);
	ui.utf8 = tty && !dumb && lang &&
		  (strstr(lang, "UTF-8") || strstr(lang, "utf-8") ||
		   strstr(lang, "UTF8") || strstr(lang, "utf8"));
	return ui;
}

struct fpal_ui fpal_ui_init(FILE *f) {
	const char *lang = getenv("LC_ALL");

	if (!lang || !*lang)
		lang = getenv("LC_CTYPE");
	if (!lang || !*lang)
		lang = getenv("LANG");
	return fpal_ui_make(isatty(fileno(f)), getenv("NO_COLOR"),
			    getenv("TERM"), lang);
}

const char *fpal_ui_style(const struct fpal_ui *ui, enum fpal_style s) {
	static const char *const codes[] = {
		[UI_RESET] = "\033[0m",	 [UI_BOLD] = "\033[1m",
		[UI_DIM] = "\033[2m",	 [UI_RED] = "\033[31m",
		[UI_GREEN] = "\033[32m", [UI_YELLOW] = "\033[33m",
		[UI_REVERSE] = "\033[7m",
	};

	return ui->color ? codes[s] : "";
}

const char *fpal_ui_mark(const struct fpal_ui *ui, enum fpal_mark m) {
	static const char *const fancy[] = { "✓", "✗", "!", "·" };
	static const char *const plain[] = { "ok", "!!", "??", "--" };

	return ui->utf8 ? fancy[m] : plain[m];
}

enum fpal_style fpal_ui_mark_style(enum fpal_mark m) {
	return m == UI_OK ? UI_GREEN : m == UI_BAD ? UI_RED :
	       m == UI_WARN ? UI_YELLOW : UI_DIM;
}

size_t fpal_ui_cols(const char *s) {
	size_t n = 0;

	for (; *s; s++)
		if (((unsigned char)*s & 0xc0) != 0x80)
			n++;
	return n;
}

void fpal_ui_pad(FILE *f, const char *s, size_t width) {
	size_t cols = fpal_ui_cols(s);

	fputs(s, f);
	for (; cols < width; cols++)
		fputc(' ', f);
}

static void rule(const struct fpal_ui *ui, FILE *f, size_t n) {
	for (size_t i = 0; i < n; i++)
		fputs(ui->utf8 ? "─" : "-", f);
}

void fpal_ui_table(const struct fpal_ui *ui, FILE *f, size_t ncols,
		   const char *const *head, size_t nrows,
		   const char *const *cells) {
	size_t width[COLS_MAX] = { 0 };

	if (ncols == 0 || ncols > COLS_MAX)
		return;
	for (size_t c = 0; c < ncols; c++) {
		width[c] = fpal_ui_cols(head[c]);
		for (size_t r = 0; r < nrows; r++) {
			const size_t w = fpal_ui_cols(cells[r * ncols + c]);

			if (w > width[c])
				width[c] = w;
		}
	}

	fputs(fpal_ui_style(ui, UI_BOLD), f);
	for (size_t c = 0; c < ncols; c++) {
		fputs("  ", f);
		/* no padding after the last column: no trailing spaces */
		fpal_ui_pad(f, head[c], c + 1 < ncols ? width[c] : 0);
	}
	fputs(fpal_ui_style(ui, UI_RESET), f);
	fputc('\n', f);

	fputs(fpal_ui_style(ui, UI_DIM), f);
	for (size_t c = 0; c < ncols; c++) {
		fputs("  ", f);
		rule(ui, f, width[c]);
	}
	fputs(fpal_ui_style(ui, UI_RESET), f);
	fputc('\n', f);

	for (size_t r = 0; r < nrows; r++) {
		for (size_t c = 0; c < ncols; c++) {
			fputs("  ", f);
			fpal_ui_pad(f, cells[r * ncols + c],
				    c + 1 < ncols ? width[c] : 0);
		}
		fputc('\n', f);
	}
}

void fpal_ui_short_id(const struct fpal_ui *ui, const struct fpal_template_id *id,
		      char *out, size_t cap) {
	const char *dots = ui->utf8 ? "…" : "...";
	const uint8_t *d = fpal_id_data(id);
	const size_t len = fpal_id_len(id);
	const size_t head = len < 16 ? len : 16;
	bool text = true;

	if (cap < 32) {
		if (cap)
			out[0] = '\0';
		return;
	}
	for (size_t i = 0; i < head; i++)
		if (d[i] < 0x20 || d[i] > 0x7e)
			text = false;
	if (text) {
		snprintf(out, cap, "%.*s%s", (int)head, (const char *)d, dots);
		return;
	}
	for (size_t i = 0; i < head / 2; i++)
		snprintf(out + 2 * i, 3, "%02x", d[i]);
	snprintf(out + 2 * (head / 2), cap - 2 * (head / 2), "%s", dots);
}

const char *fpal_ui_invoking_user(void) {
	const char *sudo_user = getenv("SUDO_USER");
	struct passwd *pw;

	if (sudo_user && *sudo_user && getpwnam(sudo_user))
		return sudo_user;
	pw = getpwuid(getuid());
	return pw ? pw->pw_name : "root";
}
