// SPDX-License-Identifier: LGPL-2.1-or-later
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "fpx/fpx.h"
#include "lock.h"

int fpx_lock_acquire(void)
{
	int fd = open(FPX_LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0600);

	if (fd < 0)
		return (errno == EACCES || errno == EPERM) ? FPX_E_PERM :
							     FPX_E_IO;

	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
		close(fd);
		return FPX_E_BUSY;
	}

	return fd;
}

void fpx_lock_release(int fd)
{
	if (fd < 0)
		return;
	flock(fd, LOCK_UN);
	close(fd);
}
