/* SPDX-License-Identifier: Apache-2.0 */
/*
 * "fpal manage": a full-screen list of the fingerprints on the sensor where
 * you pick one with the arrow keys and rename or delete it, or add a new one.
 *
 * Deleting and adding ask for a typed confirmation first: "yes" for a finger
 * fpal created, "DELETE" for one it did not (for example from Windows Hello).
 *
 * The screen logic does not touch the sensor or the terminal itself: it works
 * through fpal_manage_ops and reads keys from a file descriptor, so it can be
 * tested with a pipe and a fake sensor.
 */
#ifndef FPAL_TOOLS_MANAGE_H
#define FPAL_TOOLS_MANAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "fpal/fpal.h"
#include "ui.h"

/* ---- keys ---------------------------------------------------------------- */

enum fpal_key_type {
	K_NONE, /* something we do not use */
	K_UP,
	K_DOWN,
	K_HOME,
	K_END,
	K_ENTER,
	K_ESC,
	K_BACKSPACE,
	K_CHAR, /* a printable character, in .ch (a Unicode code point) */
};

struct fpal_key {
	enum fpal_key_type type;
	uint32_t ch;
};

/*
 * Decode one key from the start of @buf. Returns the number of bytes used, or
 * 0 if more bytes are needed (an escape sequence or a UTF-8 character that is
 * not complete). With @final set nothing more will arrive, so an unfinished
 * sequence is dropped and a lone ESC is the Escape key.
 */
size_t fpal_key_parse(const unsigned char *buf, size_t len, bool final,
		      struct fpal_key *k);

/* ---- one line of text ---------------------------------------------------- */

#define FPAL_LINE_MAX 96u

struct fpal_line {
	char text[FPAL_LINE_MAX];
	size_t len;
};

enum fpal_line_state { LINE_EDITING, LINE_DONE, LINE_CANCELLED };

void fpal_line_set(struct fpal_line *l, const char *text);

/* Enter finishes, Escape cancels, Backspace removes a whole character. */
enum fpal_line_state fpal_line_feed(struct fpal_line *l, const struct fpal_key *k);

/* ---- the screen ---------------------------------------------------------- */

struct fpal_manage_view {
	const struct fpal_template_id *ids;
	size_t n, sel;
	size_t max;	       /* how many the sensor holds */
	const char *state_dir; /* where the names live */
	const char *user;      /* whose bindings "Yours" refers to */
	bool can_write;
	const char *status; /* a message line, or NULL */
	enum fpal_mark status_mark;
	const char *prompt; /* asking for typed input, or NULL */
	const char *input;  /* what has been typed so far */
};

void fpal_manage_draw(const struct fpal_ui *ui, FILE *f,
		      const struct fpal_manage_view *v);

/* ---- the loop ------------------------------------------------------------ */

/* What the loop needs from the sensor; the command line wires these to fpal. */
struct fpal_manage_ops {
	void *ud;
	bool can_write; /* false in a read-only build: no delete, no new finger */
	int (*list)(void *ud, struct fpal_template_id *ids, size_t cap, size_t *n);
	int (*remove)(void *ud, const struct fpal_template_id *id);
	int (*new_id)(void *ud, struct fpal_template_id *id);
	size_t max; /* templates the sensor holds (at most FPAL_MAX_TEMPLATES) */
	int (*enroll)(void *ud, const struct fpal_template_id *id,
		      fpal_enroll_cb cb, void *cb_ud);
};

/*
 * Run the screen until the user quits, @in_fd reaches end of file or the
 * process is asked to stop. Writes the screen to @out. Returns 0, or the
 * error (nothing is printed) if the list could not be read at all.
 */
int fpal_manage_run(const struct fpal_manage_ops *ops, const struct fpal_ui *ui,
		    int in_fd, FILE *out, const char *state_dir, const char *user);

/* The command: puts the terminal in raw mode and runs the loop on the sensor. */
int fpal_cmd_manage(int argc, char **argv);

#endif /* FPAL_TOOLS_MANAGE_H */
