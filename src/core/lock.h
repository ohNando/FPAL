/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Process wide exclusive lock so that two clients (e.g. two sudo prompts)
 * never talk to the sensor at the same time.
 */
#ifndef FPX_CORE_LOCK_H
#define FPX_CORE_LOCK_H

/* Returns a lock fd (>= 0) or a negative fpx_err. */
int fpx_lock_acquire(void);

void fpx_lock_release(int fd);

#endif /* FPX_CORE_LOCK_H */
