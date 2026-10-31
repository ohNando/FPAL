// SPDX-License-Identifier: Apache-2.0
/*
 * Binding files: <dir>/<user>, one 64 character hex template id per line.
 * Writes go to a temporary file in the same directory which is then
 * renamed over the original, so a crash never leaves a half written file.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fpal/bindings.h"

#define USER_MAX 32u
#define HEX_LEN (FPAL_TEMPLATE_ID_LEN * 2u)

bool fpal_bindings_valid_user(const char *user) {
	size_t len;

	if (!user || !user[0] || user[0] == '-' || user[0] == '.')
		return false;

	len = strlen(user);
	if (len > USER_MAX)
		return false;

	for (size_t i = 0; i < len; i++) {
		const char c = user[i];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') || c == '_' || c == '-' ||
		      c == '.'))
			return false;
	}
	return true;
}

static int user_path(const char *dir, const char *user, char *buf, size_t cap) {
	int len;

	if (!dir || !fpal_bindings_valid_user(user))
		return FPAL_E_INVAL;
	len = snprintf(buf, cap, "%s/%s", dir, user);
	if (len < 0 || (size_t)len >= cap)
		return FPAL_E_OVERFLOW;
	return FPAL_OK;
}

static int hex_nibble(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static bool parse_hex_id(const char *s, struct fpal_template_id *id) {
	for (size_t i = 0; i < FPAL_TEMPLATE_ID_LEN; i++) {
		const int hi = hex_nibble(s[2 * i]);
		const int lo = hex_nibble(s[2 * i + 1]);

		if (hi < 0 || lo < 0)
			return false;
		id->bytes[i] = (uint8_t)(hi << 4 | lo);
	}
	return s[HEX_LEN] == '\0' || s[HEX_LEN] == '\n';
}

int fpal_bindings_load(const char *dir, const char *user,
		      struct fpal_template_id *out, size_t cap, size_t *n) {
	char path[512], line[HEX_LEN + 8];
	size_t count = 0;
	struct stat st;
	FILE *f;
	int fd, err;

	if (!n || (cap && !out))
		return FPAL_E_INVAL;
	err = user_path(dir, user, path, sizeof(path));
	if (err)
		return err;

	*n = 0;
	/* O_NOFOLLOW: never follow a symlink planted in the state dir. */
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		if (errno == ENOENT)
			return FPAL_OK;
		return (errno == EACCES || errno == EPERM) ? FPAL_E_PERM :
							     FPAL_E_IO;
	}

	/* Only trust files owned by us (root) that nobody else can modify. */
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

	while (fgets(line, sizeof(line), f)) {
		struct fpal_template_id id;

		if (line[0] == '#' || line[0] == '\n')
			continue;
		if (!parse_hex_id(line, &id))
			continue; /* ignore malformed lines */
		if (count >= cap) {
			fclose(f);
			return FPAL_E_OVERFLOW;
		}
		out[count++] = id;
	}

	fclose(f);
	*n = count;
	return FPAL_OK;
}

/* Atomically replace @user's file with @ids. */
static int store(const char *dir, const char *user,
		 const struct fpal_template_id *ids, size_t n) {
	char path[512], tmp[512];
	FILE *f;
	int fd, err;

	err = user_path(dir, user, path, sizeof(path));
	if (err)
		return err;
	if ((size_t)snprintf(tmp, sizeof(tmp), "%s/.%s.XXXXXX", dir, user) >=
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

	fputs("# fpal template ids bound to this user (one per line)\n", f);
	for (size_t i = 0; i < n; i++) {
		for (size_t b = 0; b < FPAL_TEMPLATE_ID_LEN; b++)
			fprintf(f, "%02x", ids[i].bytes[b]);
		fputc('\n', f);
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

static bool contains(const struct fpal_template_id *ids, size_t n,
		     const struct fpal_template_id *id) {
	for (size_t i = 0; i < n; i++)
		if (memcmp(ids[i].bytes, id->bytes, FPAL_TEMPLATE_ID_LEN) == 0)
			return true;
	return false;
}

int fpal_bindings_add(const char *dir, const char *user,
		     const struct fpal_template_id *id) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	size_t n = 0;
	int err;

	if (!id)
		return FPAL_E_INVAL;
	err = fpal_bindings_load(dir, user, ids, FPAL_MAX_TEMPLATES, &n);
	if (err)
		return err;
	if (contains(ids, n, id))
		return FPAL_OK;
	if (n >= FPAL_MAX_TEMPLATES)
		return FPAL_E_OVERFLOW;

	ids[n++] = *id;
	return store(dir, user, ids, n);
}

int fpal_bindings_clear(const char *dir, const char *user) {
	char path[512];
	int err = user_path(dir, user, path, sizeof(path));

	if (err)
		return err;
	if (unlink(path) < 0 && errno != ENOENT)
		return (errno == EACCES || errno == EPERM) ? FPAL_E_PERM :
							     FPAL_E_IO;
	return FPAL_OK;
}

bool fpal_bindings_contains(const char *dir, const char *user,
			   const struct fpal_template_id *id) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	size_t n = 0;

	if (!id || fpal_bindings_load(dir, user, ids, FPAL_MAX_TEMPLATES, &n))
		return false;
	return contains(ids, n, id);
}
