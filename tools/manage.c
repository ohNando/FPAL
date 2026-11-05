// SPDX-License-Identifier: Apache-2.0
#include "manage.h"

#include <errno.h>
#include <stdarg.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "finger.h"
#include "fpal/bindings.h"
#include "fpal/labels.h"

/* Set by SIGINT/SIGTERM; the loop ends and the terminal is put back. */
static volatile sig_atomic_t stop_requested;

/* ---- keys ---------------------------------------------------------------- */

static size_t utf8_len(unsigned char c) {
	if (c >= 0xc2 && c < 0xe0)
		return 2;
	if (c >= 0xe0 && c < 0xf0)
		return 3;
	if (c >= 0xf0 && c <= 0xf4)
		return 4;
	return 0;
}

size_t fpal_key_parse(const unsigned char *b, size_t len, bool final,
		      struct fpal_key *k) {
	unsigned char c;
	size_t need;

	k->type = K_NONE;
	k->ch = 0;
	if (len == 0)
		return 0;
	c = b[0];

	if (c == 0x1b) {
		size_t i = 2;

		if (len == 1) {
			if (!final)
				return 0;
			k->type = K_ESC;
			return 1;
		}
		if (b[1] != '[' && b[1] != 'O') {
			k->type = K_ESC; /* Escape, then another key */
			return 1;
		}
		/* parameters and intermediates run up to a final byte 0x40..0x7e */
		while (i < len && !(b[i] >= 0x40 && b[i] <= 0x7e))
			i++;
		if (i >= len)
			return final ? len : 0;
		switch (b[i]) {
		case 'A':
			k->type = K_UP;
			break;
		case 'B':
			k->type = K_DOWN;
			break;
		case 'H':
			k->type = K_HOME;
			break;
		case 'F':
			k->type = K_END;
			break;
		case '~': /* ESC [ 1 ~ and 7 ~ are Home, 4 ~ and 8 ~ End */
			if (i == 3 && (b[2] == '1' || b[2] == '7'))
				k->type = K_HOME;
			else if (i == 3 && (b[2] == '4' || b[2] == '8'))
				k->type = K_END;
			break;
		default:
			break;
		}
		return i + 1;
	}
	if (c == '\r' || c == '\n') {
		k->type = K_ENTER;
		return (c == '\r' && len > 1 && b[1] == '\n') ? 2 : 1;
	}
	if (c == 0x7f || c == 0x08) {
		k->type = K_BACKSPACE;
		return 1;
	}
	if (c < 0x20)
		return 1;
	if (c < 0x80) {
		k->type = K_CHAR;
		k->ch = c;
		return 1;
	}

	need = utf8_len(c);
	if (!need)
		return 1; /* a stray continuation byte */
	if (len < need)
		return final ? len : 0;
	k->ch = need == 2 ? c & 0x1f : need == 3 ? c & 0x0f : c & 0x07;
	for (size_t i = 1; i < need; i++) {
		if ((b[i] & 0xc0) != 0x80) {
			k->ch = 0;
			return i; /* broken sequence: drop what was read */
		}
		k->ch = k->ch << 6 | (b[i] & 0x3f);
	}
	k->type = K_CHAR;
	return need;
}

/* ---- one line of text ---------------------------------------------------- */

void fpal_line_set(struct fpal_line *l, const char *text) {
	l->len = strnlen(text, sizeof(l->text) - 1);
	memcpy(l->text, text, l->len);
	l->text[l->len] = '\0';
}

static size_t utf8_put(char *out, uint32_t ch) {
	if (ch < 0x80) {
		out[0] = (char)ch;
		return 1;
	}
	if (ch < 0x800) {
		out[0] = (char)(0xc0 | ch >> 6);
		out[1] = (char)(0x80 | (ch & 0x3f));
		return 2;
	}
	if (ch < 0x10000) {
		out[0] = (char)(0xe0 | ch >> 12);
		out[1] = (char)(0x80 | (ch >> 6 & 0x3f));
		out[2] = (char)(0x80 | (ch & 0x3f));
		return 3;
	}
	out[0] = (char)(0xf0 | ch >> 18);
	out[1] = (char)(0x80 | (ch >> 12 & 0x3f));
	out[2] = (char)(0x80 | (ch >> 6 & 0x3f));
	out[3] = (char)(0x80 | (ch & 0x3f));
	return 4;
}

enum fpal_line_state fpal_line_feed(struct fpal_line *l, const struct fpal_key *k) {
	char enc[4];
	size_t n;

	switch (k->type) {
	case K_ENTER:
		return LINE_DONE;
	case K_ESC:
		return LINE_CANCELLED;
	case K_BACKSPACE:
		while (l->len > 0) {
			const bool lead = ((unsigned char)l->text[l->len - 1] & 0xc0) != 0x80;

			l->text[--l->len] = '\0';
			if (lead)
				break;
		}
		break;
	case K_CHAR:
		if (!fpal_label_char_ok(k->ch))
			break;
		n = utf8_put(enc, k->ch);
		if (l->len + n < sizeof(l->text)) {
			memcpy(l->text + l->len, enc, n);
			l->len += n;
			l->text[l->len] = '\0';
		}
		break;
	default:
		break;
	}
	return LINE_EDITING;
}

/* ---- the screen ---------------------------------------------------------- */

#define NAME_W 22u

static void draw_row(const struct fpal_ui *ui, FILE *f,
		     const struct fpal_manage_view *v, size_t i) {
	const struct fpal_template_id *id = &v->ids[i];
	const bool sel = i == v->sel;
	char label[FPAL_LABEL_MAX + 1] = "", sid[40];
	const bool mine = v->user &&
			  fpal_bindings_contains(v->state_dir, v->user, id);

	(void)fpal_labels_get(v->state_dir, id, label, sizeof(label));
	fpal_ui_short_id(ui, id, sid, sizeof(sid));

	fputs(sel ? fpal_ui_style(ui, UI_REVERSE) : "", f);
	fprintf(f, "  %s %zu  ", sel ? (ui->utf8 ? "▸" : ">") : " ", i);
	fpal_ui_pad(f, label[0] ? label : "(no name)", NAME_W);
	fputs("  ", f);
	fpal_ui_pad(f, fpal_finger_is_ours(id) ? "FPAL" : "other", 5);
	fputs("  ", f);
	fpal_ui_pad(f, mine ? fpal_ui_mark(ui, UI_OK) : "", 5);
	fputs("  ", f);
	fpal_ui_pad(f, sid, 20);
	fputs(fpal_ui_style(ui, UI_RESET), f);
	fputc('\n', f);
}

void fpal_manage_draw(const struct fpal_ui *ui, FILE *f,
		      const struct fpal_manage_view *v) {
	fputs("\033[H\033[2J", f);
	fprintf(f, "\n  %sfpal manage%s   %s%zu of %u fingerprints%s\n\n",
		fpal_ui_style(ui, UI_BOLD), fpal_ui_style(ui, UI_RESET),
		fpal_ui_style(ui, UI_DIM), v->n, FPAL_MAX_TEMPLATES,
		fpal_ui_style(ui, UI_RESET));

	fputs(fpal_ui_style(ui, UI_DIM), f);
	fputs("      #  ", f);
	fpal_ui_pad(f, "Name", NAME_W);
	fputs("  ", f);
	fpal_ui_pad(f, "Kind", 5);
	fputs("  ", f);
	fpal_ui_pad(f, "Yours", 5);
	fputs("  Id", f);
	fputs(fpal_ui_style(ui, UI_RESET), f);
	fputc('\n', f);
	for (size_t i = 0; i < v->n; i++)
		draw_row(ui, f, v, i);
	if (v->n == 0)
		fprintf(f, "  %s(no fingerprints on the sensor)%s\n",
			fpal_ui_style(ui, UI_DIM), fpal_ui_style(ui, UI_RESET));

	fputc('\n', f);
	if (v->status)
		fprintf(f, "  %s%s%s %s\n", fpal_ui_style(ui, fpal_ui_mark_style(v->status_mark)),
			fpal_ui_mark(ui, v->status_mark), fpal_ui_style(ui, UI_RESET),
			v->status);
	else
		fputc('\n', f);
	fputc('\n', f);

	if (v->prompt) {
		fprintf(f, "  %s%s%s %s%s\n", fpal_ui_style(ui, UI_BOLD), v->prompt,
			fpal_ui_style(ui, UI_RESET), v->input ? v->input : "",
			ui->utf8 ? "▏" : "_");
		fprintf(f, "  %sEnter confirms, Esc cancels%s\n",
			fpal_ui_style(ui, UI_DIM), fpal_ui_style(ui, UI_RESET));
	} else {
		fprintf(f, "  %s%s select   r rename%s%s   q quit%s\n",
			fpal_ui_style(ui, UI_DIM), ui->utf8 ? "↑/↓" : "up/down",
			v->can_write ? "   d delete   n new finger" : "",
			"", fpal_ui_style(ui, UI_RESET));
	}
	fflush(f);
}

/* ---- the loop ------------------------------------------------------------ */

enum mode {
	M_LIST,
	M_RENAME,
	M_CONFIRM_DELETE,
	M_CONFIRM_NEW,
	M_NAME_NEW,
};

struct app {
	const struct fpal_manage_ops *ops;
	const struct fpal_ui *ui;
	FILE *out;
	const char *state_dir, *user;

	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	size_t n, sel;
	enum mode mode;
	struct fpal_line line;
	struct fpal_template_id fresh; /* the finger just enrolled */
	char status[160];
	bool has_status;
	enum fpal_mark mark;
};

static void say(struct app *a, enum fpal_mark m, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

static void say(struct app *a, enum fpal_mark m, const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(a->status, sizeof(a->status), fmt, ap);
	va_end(ap);
	a->has_status = true;
	a->mark = m;
}

static void draw(struct app *a) {
	const char *prompt = NULL;
	struct fpal_manage_view v = {
		.ids = a->ids, .n = a->n, .sel = a->sel,
		.state_dir = a->state_dir, .user = a->user,
		.can_write = a->ops->can_write,
		.status = a->has_status ? a->status : NULL,
		.status_mark = a->mark,
		.input = a->line.text,
	};
	char buf[160];

	switch (a->mode) {
	case M_RENAME:
		prompt = "New name (empty removes it):";
		break;
	case M_CONFIRM_DELETE:
		snprintf(buf, sizeof(buf), fpal_finger_is_ours(&a->ids[a->sel]) ?
			 "Delete finger %zu for good? Type yes:" :
			 "Finger %zu was NOT created by fpal (Windows Hello?). Type DELETE:",
			 a->sel);
		prompt = buf;
		break;
	case M_CONFIRM_NEW:
		prompt = "Add a new finger for you? Type yes:";
		break;
	case M_NAME_NEW:
		prompt = "Name for the new finger (Enter to skip):";
		break;
	default:
		break;
	}
	v.prompt = prompt;
	fpal_manage_draw(a->ui, a->out, &v);
}

static int refresh(struct app *a) {
	int err = a->ops->list(a->ops->ud, a->ids, FPAL_MAX_TEMPLATES, &a->n);

	if (err) {
		a->n = 0;
		return err;
	}
	if (a->sel >= a->n)
		a->sel = a->n ? a->n - 1 : 0;
	return FPAL_OK;
}

static void enter_mode(struct app *a, enum mode m, const char *preset) {
	a->mode = m;
	fpal_line_set(&a->line, preset);
}

static void do_rename(struct app *a) {
	const char *name = a->line.text;
	int err;

	if (name[0] && !fpal_label_valid(name)) {
		say(a, UI_BAD, "not a usable name (1 to %u characters, no control characters)",
		    FPAL_LABEL_MAX);
		return;
	}
	err = fpal_labels_set(a->state_dir, &a->ids[a->sel], name[0] ? name : NULL);
	if (err)
		say(a, UI_BAD, "could not save the name: %s", fpal_strerror(err));
	else if (name[0])
		say(a, UI_OK, "finger %zu is now called \"%s\"", a->sel, name);
	else
		say(a, UI_OK, "name of finger %zu removed", a->sel);
}

static void do_delete(struct app *a) {
	const struct fpal_template_id victim = a->ids[a->sel];
	const char *phrase = fpal_finger_is_ours(&victim) ? "yes" : "DELETE";
	int err;

	if (strcmp(a->line.text, phrase) != 0) {
		say(a, UI_WARN, "not confirmed, nothing was deleted");
		return;
	}
	err = a->ops->remove(a->ops->ud, &victim);
	if (err == FPAL_E_NOTSUP) {
		say(a, UI_BAD, "this is a read-only build");
		return;
	}
	if (err) {
		say(a, UI_BAD, "not deleted: %s", fpal_strerror(err));
		(void)refresh(a);
		return;
	}
	/* it is gone from the sensor: drop what pointed at it */
	(void)fpal_bindings_forget(a->state_dir, &victim);
	(void)fpal_labels_set(a->state_dir, &victim, NULL);
	(void)refresh(a);
	say(a, UI_OK, "deleted; %zu fingerprint(s) left on the sensor", a->n);
}

static void on_progress(enum fpal_enroll_event ev, uint8_t stage, uint8_t total,
			void *ud) {
	struct app *a = ud;

	switch (ev) {
	case FPAL_ENROLL_CHECK_TOUCH:
		say(a, UI_INFO, "touch the sensor once to check that this finger is new");
		break;
	case FPAL_ENROLL_TOUCH:
		say(a, UI_INFO, "scan %u of %u: touch the sensor", stage + 1u, total);
		break;
	case FPAL_ENROLL_STAGE_DONE:
		say(a, UI_OK, "accepted (%u/%u)", stage, total);
		break;
	case FPAL_ENROLL_RETRY_CENTER:
		say(a, UI_WARN, "finger off centre, touch again");
		break;
	case FPAL_ENROLL_RETRY_DIRTY:
		say(a, UI_WARN, "the sensor looks dirty: clean it and touch again");
		break;
	case FPAL_ENROLL_RETRY:
		say(a, UI_WARN, "could not read that, lift your finger and touch again");
		break;
	}
	draw(a);
}

static void do_new(struct app *a) {
	int err;

	if (strcmp(a->line.text, "yes") != 0) {
		say(a, UI_WARN, "not confirmed, nothing was stored");
		return;
	}
	err = a->ops->new_id(&a->fresh);
	if (!err)
		err = a->ops->enroll(a->ops->ud, &a->fresh, on_progress, a);
	if (err == FPAL_E_NOTSUP) {
		say(a, UI_BAD, "this is a read-only build");
		return;
	}
	if (err) {
		say(a, UI_BAD, "not stored: %s", err == FPAL_E_EXISTS ?
			"this finger is already enrolled" : fpal_strerror(err));
		(void)refresh(a);
		return;
	}
	err = fpal_bindings_add(a->state_dir, a->user, &a->fresh);
	(void)refresh(a);
	for (size_t i = 0; i < a->n; i++)
		if (fpal_id_equal(&a->ids[i], &a->fresh))
			a->sel = i;
	if (err)
		say(a, UI_WARN, "stored, but could not bind it to %s: %s", a->user,
		    fpal_strerror(err));
	else
		say(a, UI_OK, "new finger stored and bound to %s", a->user);
	enter_mode(a, M_NAME_NEW, "");
}

static void do_name_new(struct app *a) {
	const char *name = a->line.text;

	if (!name[0])
		return;
	if (!fpal_label_valid(name))
		say(a, UI_BAD, "not a usable name, use r to try again");
	else if (fpal_labels_set(a->state_dir, &a->fresh, name))
		say(a, UI_BAD, "could not save the name");
	else
		say(a, UI_OK, "new finger is called \"%s\"", name);
}

/* Buffered key reader on a file descriptor. */
struct input {
	int fd;
	unsigned char buf[32];
	size_t len;
};

/* 1 with a key, 0 at end of input or when asked to stop. */
static int next_key(struct input *in, struct fpal_key *k) {
	for (;;) {
		size_t used;
		ssize_t r;

		if (stop_requested)
			return 0;
		used = fpal_key_parse(in->buf, in->len, false, k);
		if (!used && in->len) {
			/* a lone Escape, or the start of a sequence: wait briefly */
			struct pollfd p = { .fd = in->fd, .events = POLLIN };

			if (poll(&p, 1, 50) == 0)
				used = fpal_key_parse(in->buf, in->len, true, k);
		}
		if (used) {
			if (used > in->len)
				used = in->len;
			memmove(in->buf, in->buf + used, in->len - used);
			in->len -= used;
			return 1;
		}
		{
			/* A fixed size read, then a bounded copy: no size to get wrong. */
			unsigned char chunk[16];
			size_t room = sizeof(in->buf) > in->len ? sizeof(in->buf) - in->len : 0;

			if (!room) {
				in->len = 0; /* never happens with real keys; do not wedge */
				room = sizeof(in->buf);
			}
			r = read(in->fd, chunk, sizeof(chunk));
			if (r > 0) {
				size_t take = (size_t)r < room ? (size_t)r : room;

				memcpy(in->buf + in->len, chunk, take);
				in->len += take;
			} else if (r == 0 || errno != EINTR) {
				return 0;
			}
		}
	}
}

int fpal_manage_run(const struct fpal_manage_ops *ops, const struct fpal_ui *ui,
		    int in_fd, FILE *out, const char *state_dir, const char *user) {
	struct app a = { .ops = ops, .ui = ui, .out = out,
			 .state_dir = state_dir, .user = user };
	struct input in = { .fd = in_fd };
	struct fpal_key k;
	int err = refresh(&a);

	if (err)
		return err; /* the caller says so once the normal screen is back */

	for (;;) {
		enum fpal_line_state st;

		draw(&a);
		if (!next_key(&in, &k))
			break;
		a.has_status = false;

		if (a.mode == M_LIST) {
			if (k.type == K_UP || (k.type == K_CHAR && k.ch == 'k')) {
				if (a.sel > 0)
					a.sel--;
			} else if (k.type == K_DOWN || (k.type == K_CHAR && k.ch == 'j')) {
				if (a.sel + 1 < a.n)
					a.sel++;
			} else if (k.type == K_HOME) {
				a.sel = 0;
			} else if (k.type == K_END) {
				a.sel = a.n ? a.n - 1 : 0;
			} else if (k.type == K_ESC || (k.type == K_CHAR && k.ch == 'q')) {
				break;
			} else if (k.type == K_CHAR && k.ch == 'r') {
				char cur[FPAL_LABEL_MAX + 1] = "";

				if (!a.n) {
					say(&a, UI_WARN, "no finger to rename");
				} else {
					(void)fpal_labels_get(state_dir, &a.ids[a.sel], cur, sizeof(cur));
					enter_mode(&a, M_RENAME, cur);
				}
			} else if (k.type == K_CHAR && k.ch == 'd') {
				if (!ops->can_write)
					say(&a, UI_WARN, "this is a read-only build, it cannot delete");
				else if (!a.n)
					say(&a, UI_WARN, "no finger to delete");
				else
					enter_mode(&a, M_CONFIRM_DELETE, "");
			} else if (k.type == K_CHAR && k.ch == 'n') {
				if (!ops->can_write)
					say(&a, UI_WARN, "this is a read-only build, it cannot add a finger");
				else if (a.n >= FPAL_MAX_TEMPLATES)
					say(&a, UI_WARN, "the sensor is full (%u fingerprints)", FPAL_MAX_TEMPLATES);
				else
					enter_mode(&a, M_CONFIRM_NEW, "");
			}
			continue;
		}

		st = fpal_line_feed(&a.line, &k);
		if (st == LINE_EDITING)
			continue;
		if (st == LINE_CANCELLED) {
			say(&a, UI_WARN, "cancelled, nothing was changed");
		} else {
			switch (a.mode) {
			case M_RENAME:
				do_rename(&a);
				break;
			case M_CONFIRM_DELETE:
				do_delete(&a);
				break;
			case M_CONFIRM_NEW:
				do_new(&a);
				continue; /* do_new may have moved on to naming */
			case M_NAME_NEW:
				do_name_new(&a);
				break;
			default:
				break;
			}
		}
		enter_mode(&a, M_LIST, "");
	}
	return 0;
}

/* ---- the command --------------------------------------------------------- */

static struct fpal_device *volatile active_dev;

static void on_signal(int sig) {
	(void)sig;
	stop_requested = 1;
	if (active_dev)
		fpal_cancel(active_dev);
}

static int sensor_list(void *ud, struct fpal_template_id *ids, size_t cap, size_t *n) {
	struct fpal_device *dev;
	int err;

	(void)ud;
	err = fpal_device_open_first(&dev);
	if (err)
		return err;
	err = fpal_list(dev, ids, cap, n);
	fpal_device_close(dev);
	return err;
}

static int sensor_remove(void *ud, const struct fpal_template_id *id) {
	struct fpal_device *dev;
	int err;

	(void)ud;
	err = fpal_device_open_first(&dev);
	if (err)
		return err;
	err = fpal_delete(dev, id);
	fpal_device_close(dev);
	return err;
}

static int sensor_enroll(void *ud, const struct fpal_template_id *id,
			 fpal_enroll_cb cb, void *cb_ud) {
	struct fpal_device *dev;
	int err;

	(void)ud;
	err = fpal_device_open_first(&dev);
	if (err)
		return err;
	active_dev = dev; /* Ctrl-C cancels the finger wait */
	err = fpal_enroll(dev, id, 0, cb, cb_ud);
	active_dev = NULL;
	fpal_device_close(dev);
	return err;
}

static struct termios saved_tty;
static bool tty_saved;

static void leave_screen(void) {
	if (!tty_saved)
		return;
	fputs("\033[?25h\033[?1049l", stdout);
	fflush(stdout);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_tty);
	tty_saved = false;
}

int fpal_cmd_manage(int argc, char **argv) {
	struct fpal_manage_ops ops = {
		.can_write =
#ifdef FPAL_ENABLE_ENROLL
			true,
#else
			false,
#endif
		.list = sensor_list, .remove = sensor_remove,
		.new_id = fpal_template_id_generate, .enroll = sensor_enroll,
	};
	struct sigaction sa = { .sa_handler = on_signal };
	struct fpal_ui ui = fpal_ui_init(stdout);
	struct termios raw;
	const char *term = getenv("TERM");
	int rc;

	(void)argv;
	if (argc != 1) {
		fputs("usage: fpal manage\n", stderr);
		return 2;
	}
	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) ||
	    (term && !strcmp(term, "dumb"))) {
		fputs("fpal: manage needs a terminal; in scripts use 'fpal list'\n",
		      stderr);
		return 2;
	}

	/* No SA_RESTART: a signal must interrupt the key read. */
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	if (tcgetattr(STDIN_FILENO, &saved_tty) < 0) {
		fputs("fpal: cannot read the terminal settings\n", stderr);
		return 1;
	}
	tty_saved = true;
	atexit(leave_screen);
	raw = saved_tty;
	raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | IEXTEN); /* keep ISIG: Ctrl-C works */
	raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL | INLCR);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	fputs("\033[?1049h\033[?25l", stdout);

	rc = fpal_manage_run(&ops, &ui, STDIN_FILENO, stdout, FPAL_STATE_DIR,
			     fpal_ui_invoking_user());
	leave_screen();
	if (rc) {
		if (rc == FPAL_E_PERM && geteuid() != 0)
			fputs("fpal: manage needs root: sudo fpal manage\n", stderr);
		else
			fprintf(stderr, "fpal: cannot read the sensor: %s\n",
				fpal_strerror(rc));
		return 1;
	}
	return 0;
}
