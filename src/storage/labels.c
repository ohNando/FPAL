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

#define HEX_LEN (FPAL_TEMPLATE_ID_LEN * 2u)
#define LINE_MAX_LEN (HEX_LEN + 1u + FPAL_LABEL_MAX + 2u)

struct entry {
	struct fpal_template_id id;
	char label[FPAL_LABEL_MAX + 1];
};

bool fpal_label_valid(const char *label) {
	size_t len;

	if (!label)
		return false;
	len = strlen(label);
	if (len == 0 || len > FPAL_LABEL_MAX)
		return false;
	if (label[0] == ' ' || label[len - 1] == ' ')
		return false;
	for (size_t i = 0; i < len; i++) {
		const unsigned char c = (unsigned char)label[i];

		if (c < 0x20 || c == 0x7f)
			return false;
	}
	return true;
}

static int path_of(const char *dir, char *buf, size_t cap) {
	int len;

	if (!dir)
		return FPAL_E_INVAL;
	len = snprintf(buf, cap, "%s/.labels", dir);
	return (len < 0 || (size_t)len >= cap) ? FPAL_E_OVERFLOW : FPAL_OK;
}

static int hex_nibble(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

static bool parse_id(const char *s, struct fpal_template_id *id) {
	for (size_t i = 0; i < FPAL_TEMPLATE_ID_LEN; i++) {
		const int hi = hex_nibble(s[2 * i]);
		const int lo = hex_nibble(s[2 * i + 1]);

		if (hi < 0 || lo < 0)
			return false;
		id->bytes[i] = (uint8_t)(hi << 4 | lo);
	}
	return true;
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

		if (!nl)
			continue; /* over long line */
		*nl = '\0';
		if (strlen(line) <= HEX_LEN + 1 || line[HEX_LEN] != '\t' ||
		    !parse_id(line, &cur->id) ||
		    !fpal_label_valid(line + HEX_LEN + 1))
			continue; /* ignore malformed lines */
		strcpy(cur->label, line + HEX_LEN + 1);
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
		for (size_t b = 0; b < FPAL_TEMPLATE_ID_LEN; b++)
			fprintf(f, "%02x", e[i].id.bytes[b]);
		fprintf(f, "\t%s\n", e[i].label);
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
		if (!memcmp(e[i].id.bytes, id->bytes, FPAL_TEMPLATE_ID_LEN))
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
