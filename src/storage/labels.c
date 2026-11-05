// SPDX-License-Identifier: Apache-2.0
/*
 * Label file: <dir>/.labels, lines of "<64 hex id>\t<name>". Written the
 * same careful way as the binding files: no symlinks, owned by us, replaced
 * atomically. A leading dot keeps it apart from user files, since user names
 * cannot start with one.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fpal/labels.h"

#define LINE_MAX_LEN (FPAL_ID_HEX_MAX + 1u + FPAL_LABEL_MAX + 2u)

struct entry {
	struct fpal_template_id id;
	char label[FPAL_LABEL_MAX + 1];
};

bool fpal_label_char_ok(uint32_t cp) {
	if (cp < 0x20 || (cp >= 0x7f && cp <= 0x9f))
		return false; /* C0, DEL, C1 */
	if (cp >= 0xd800 && cp <= 0xdfff)
		return false; /* surrogates */
	if (cp > 0x10ffff)
		return false;
	if ((cp >= 0x200b && cp <= 0x200f) ||	/* zero width, direction marks */
	    (cp >= 0x2028 && cp <= 0x202e) ||	/* separators, bidi embeddings */
	    (cp >= 0x2060 && cp <= 0x2069) ||	/* word joiner, bidi isolates */
	    cp == 0xfeff ||			/* BOM */
	    (cp >= 0xfff9 && cp <= 0xfffb))	/* interlinear annotation */
		return false;
	return true;
}

/* Strict UTF-8: no overlong forms, no continuation byte out of place. */
static bool utf8_ok(const unsigned char *p) {
	while (*p) {
		uint32_t cp;
		size_t n;

		if (p[0] < 0x80) {
			cp = p[0];
			n = 1;
		} else if (p[0] >= 0xc2 && p[0] <= 0xdf) {
			cp = p[0] & 0x1f;
			n = 2;
		} else if (p[0] >= 0xe0 && p[0] <= 0xef) {
			cp = p[0] & 0x0f;
			n = 3;
		} else if (p[0] >= 0xf0 && p[0] <= 0xf4) {
			cp = p[0] & 0x07;
			n = 4;
		} else {
			return false;
		}
		for (size_t i = 1; i < n; i++) {
			if ((p[i] & 0xc0) != 0x80)
				return false; /* also catches the string ending early */
			cp = cp << 6 | (p[i] & 0x3f);
		}
		if ((n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000))
			return false; /* overlong */
		if (!fpal_label_char_ok(cp))
			return false;
		p += n;
	}
	return true;
}

bool fpal_label_valid(const char *label) {
	size_t len;

	if (!label)
		return false;
	len = strlen(label);
	if (len == 0 || len > FPAL_LABEL_MAX)
		return false;
	if (label[0] == ' ' || label[len - 1] == ' ')
		return false;
	return utf8_ok((const unsigned char *)label);
}

static int path_of(const char *dir, char *buf, size_t cap) {
	int len;

	if (!dir)
		return FPAL_E_INVAL;
	len = snprintf(buf, cap, "%s/.labels", dir);
	return (len < 0 || (size_t)len >= cap) ? FPAL_E_OVERFLOW : FPAL_OK;
}

/* "<hex id>\t<name>": split at the tab, the id is lower case hex. */
static bool parse_line(char *line, struct fpal_template_id *id, const char **label) {
	char *tab = strchr(line, '\t');

	if (!tab || fpal_id_from_hex(line, (size_t)(tab - line), false, id) != FPAL_OK)
		return false;
	*label = tab + 1;
	return fpal_label_valid(*label);
}

static int load(const char *dir, struct entry *e, size_t *n) {
	char path[512], line[LINE_MAX_LEN + 1];
	size_t count = 0;
	struct stat st;
	FILE *f;
	int fd, err;

	*n = 0;
	err = path_of(dir, path, sizeof(path));
	if (err)
		return err;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		if (errno == ENOENT)
			return FPAL_OK;
		/* ELOOP: a symlink where the file should be, not to be trusted */
		return (errno == EACCES || errno == EPERM || errno == ELOOP) ?
			       FPAL_E_PERM :
			       FPAL_E_IO;
	}
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) ||
	    st.st_uid != geteuid() || (st.st_mode & 022)) {
		close(fd);
		return FPAL_E_PERM;
	}
	f = fdopen(fd, "r");
	if (!f) {
		close(fd);
		return FPAL_E_NOMEM;
	}

	while (fgets(line, sizeof(line), f) && count < FPAL_LABELS_MAX) {
		char *nl = strchr(line, '\n');
		struct entry *cur = &e[count];
		const char *label;

		if (!nl) {
			if (strlen(line) == sizeof(line) - 1) {
				int c; /* drain: a fragment is not a line */

				while ((c = fgetc(f)) != EOF && c != '\n')
					;
			}
			continue; /* over long line */
		}
		*nl = '\0';
		if (!parse_line(line, &cur->id, &label))
			continue; /* ignore malformed lines */
		strcpy(cur->label, label);
		count++;
	}
	fclose(f);
	*n = count;
	return FPAL_OK;
}

static int store(const char *dir, const struct entry *e, size_t n) {
	char path[512], tmp[512];
	FILE *f;
	int fd, err;

	err = path_of(dir, path, sizeof(path));
	if (err)
		return err;
	if ((size_t)snprintf(tmp, sizeof(tmp), "%s/.labels.XXXXXX", dir) >=
	    sizeof(tmp))
		return FPAL_E_OVERFLOW;
	if (mkdir(dir, 0700) < 0 && errno != EEXIST)
		return (errno == EACCES || errno == EPERM) ? FPAL_E_PERM :
							     FPAL_E_IO;
	fd = mkostemp(tmp, O_CLOEXEC);
	if (fd < 0)
		return (errno == EACCES || errno == EPERM) ? FPAL_E_PERM :
							     FPAL_E_IO;
	fchmod(fd, 0600);
	f = fdopen(fd, "w");
	if (!f) {
		close(fd);
		unlink(tmp);
		return FPAL_E_NOMEM;
	}
	for (size_t i = 0; i < n; i++) {
		char hex[FPAL_ID_HEX_MAX + 1];

		(void)fpal_id_to_hex(&e[i].id, hex, sizeof(hex));
		fprintf(f, "%s\t%s\n", hex, e[i].label);
	}
	if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
		fclose(f);
		unlink(tmp);
		return FPAL_E_IO;
	}
	fclose(f);
	if (rename(tmp, path) < 0) {
		unlink(tmp);
		return FPAL_E_IO;
	}
	return FPAL_OK;
}

static size_t index_of(const struct entry *e, size_t n,
		       const struct fpal_template_id *id) {
	for (size_t i = 0; i < n; i++)
		if (fpal_id_equal(&e[i].id, id))
			return i;
	return n;
}

int fpal_labels_get(const char *dir, const struct fpal_template_id *id,
		    char *out, size_t cap) {
	struct entry e[FPAL_LABELS_MAX];
	size_t n, i;
	int err;

	if (!id || !out || cap == 0)
		return FPAL_E_INVAL;
	out[0] = '\0';
	err = load(dir, e, &n);
	if (err)
		return err;
	i = index_of(e, n, id);
	if (i == n)
		return FPAL_OK;
	if (strlen(e[i].label) >= cap)
		return FPAL_E_OVERFLOW;
	strcpy(out, e[i].label);
	return FPAL_OK;
}

int fpal_labels_set(const char *dir, const struct fpal_template_id *id,
		    const char *label) {
	struct entry e[FPAL_LABELS_MAX];
	const bool remove = !label || !label[0];
	size_t n, i;
	int err;

	if (!id || (!remove && !fpal_label_valid(label)))
		return FPAL_E_INVAL;
	err = load(dir, e, &n);
	if (err)
		return err;
	i = index_of(e, n, id);

	if (remove) {
		if (i == n)
			return FPAL_OK;
		memmove(&e[i], &e[i + 1], (n - i - 1) * sizeof(e[0]));
		return store(dir, e, n - 1);
	}
	if (i == n) {
		if (n >= FPAL_LABELS_MAX)
			return FPAL_E_OVERFLOW;
		e[n].id = *id;
		n++;
	}
	strcpy(e[i].label, label);
	return store(dir, e, n);
}

int fpal_labels_find(const char *dir, const char *label,
		     struct fpal_template_id *id) {
	struct entry e[FPAL_LABELS_MAX];
	size_t n;
	int err;

	if (!label || !id)
		return FPAL_E_INVAL;
	err = load(dir, e, &n);
	if (err)
		return err;
	for (size_t i = 0; i < n; i++) {
		if (!strcmp(e[i].label, label)) {
			*id = e[i].id;
			return FPAL_OK;
		}
	}
	return FPAL_E_NODATA;
}
