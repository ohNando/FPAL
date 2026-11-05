// SPDX-License-Identifier: Apache-2.0
/*
 * Binding files: <dir>/<user>, one 64 character hex template id per line.
 * Writes go to a temporary file in the same directory which is then
 * renamed over the original, so a crash never leaves a half written file.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fpal/bindings.h"

#define USER_MAX 32u

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

/* One line of a binding file: the hex digits of an id, then the newline. */
static bool parse_line(const char *line, struct fpal_template_id *id) {
	return fpal_id_from_hex(line, strcspn(line, "\n"), true, id) == FPAL_OK;
}

int fpal_bindings_load(const char *dir, const char *user,
		      struct fpal_template_id *out, size_t cap, size_t *n) {
	char path[512], line[FPAL_ID_HEX_MAX + 8];
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
		if (!parse_line(line, &id))
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
		char hex[FPAL_ID_HEX_MAX + 1];

		(void)fpal_id_to_hex(&ids[i], hex, sizeof(hex));
		fprintf(f, "%s\n", hex);
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
		if (fpal_id_equal(&ids[i], id))
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

int fpal_bindings_remove(const char *dir, const char *user,
			 const struct fpal_template_id *id) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	size_t n = 0, kept = 0;
	int err;

	if (!id)
		return FPAL_E_INVAL;
	err = fpal_bindings_load(dir, user, ids, FPAL_MAX_TEMPLATES, &n);
	if (err)
		return err;
	for (size_t i = 0; i < n; i++)
		if (!fpal_id_equal(&ids[i], id))
			ids[kept++] = ids[i];
	if (kept == n)
		return FPAL_OK; /* it was not bound: leave the file alone */
	return store(dir, user, ids, kept);
}

int fpal_bindings_forget(const char *dir, const struct fpal_template_id *id) {
	struct dirent *de;
	DIR *d;
	int first = FPAL_OK;

	if (!dir || !id)
		return FPAL_E_INVAL;
	d = opendir(dir);
	if (!d)
		return errno == ENOENT ? FPAL_OK : FPAL_E_IO;
	while ((de = readdir(d))) {
		int err;

		/* skips ".", ".." and the labels file: user names never start with a dot */
		if (!fpal_bindings_valid_user(de->d_name))
			continue;
		err = fpal_bindings_remove(dir, de->d_name, id);
		if (err && first == FPAL_OK)
			first = err;
	}
	closedir(d);
	return first;
}
