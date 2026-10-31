/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Process wide exclusive lock so that two clients (e.g. two sudo prompts)
 * never talk to the sensor at the same time.
 */
#ifndef FPAL_CORE_LOCK_H
#define FPAL_CORE_LOCK_H

/* Returns a lock fd (>= 0) or a negative fpal_err. */
int fpal_lock_acquire(void);

void fpal_lock_release(int fd);

#endif /* FPAL_CORE_LOCK_H */
