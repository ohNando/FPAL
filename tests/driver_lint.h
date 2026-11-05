/* SPDX-License-Identifier: Apache-2.0 */
/*
 * The rules every driver has to follow, checked the same way for the ones
 * in the real table and for the test driver.
 */
#ifndef FPAL_TESTS_DRIVER_LINT_H
#define FPAL_TESTS_DRIVER_LINT_H

#include <string.h>

#include "core/driver.h"
#include "t.h"

static inline void driver_lint(const struct fpal_driver *drv) {
	T_CHECK(drv->name && drv->name[0]);
	T_CHECK(drv->ids && drv->ids[0].vid);
	T_CHECK(drv->open && drv->close && drv->list && drv->identify);
	T_CHECK(drv->max_templates >= 1 && drv->max_templates <= FPAL_MAX_TEMPLATES);
	/* enough random data that a stale binding cannot match another finger */
	T_CHECK(drv->new_id_len >= 16 && drv->new_id_len <= FPAL_ID_MAX);
	/* writing is all or nothing: both ops or neither */
	T_CHECK((drv->enroll == NULL) == (drv->delete_template == NULL));
}

#endif /* FPAL_TESTS_DRIVER_LINT_H */
