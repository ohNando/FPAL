// SPDX-License-Identifier: Apache-2.0
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "fpal/fpal.h"
#include "lock.h"

int fpal_lock_acquire(void) {
	int fd = open(FPAL_LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0600);

	if (fd < 0)
		return (errno == EACCES || errno == EPERM) ? FPAL_E_PERM :
							     FPAL_E_IO;

	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
		close(fd);
		return FPAL_E_BUSY;
	}

	return fd;
}

void fpal_lock_release(int fd) {
	if (fd < 0)
		return;
	flock(fd, LOCK_UN);
	close(fd);
}
