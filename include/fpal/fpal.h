/* SPDX-License-Identifier: Apache-2.0 */
/*
 * fpal public API.
 */
#ifndef FPAL_FPAL_H
#define FPAL_FPAL_H

#include "config.h"
#include "types.h"

struct fpal_device;

/* Location and identity of a supported sensor found on the USB bus. */
struct fpal_usb_info {
	uint8_t busnum;
	uint8_t devnum;
	uint16_t vid;
	uint16_t pid;
	char product[FPAL_STR_MAX];
	char serial[FPAL_STR_MAX];
	const char *driver_name;
};

/* Human readable message for an fpal_err value. */
const char *fpal_strerror(int err);

/*
 * Scan the USB bus for supported sensors. Fills up to @cap entries of @out
 * and stores the number found in @n. Does not open the devices.
 */
int fpal_scan(struct fpal_usb_info *out, size_t cap, size_t *n);

/* Open the first supported sensor. Requires root. */
int fpal_device_open_first(struct fpal_device **out);

void fpal_device_close(struct fpal_device *dev);

const char *fpal_device_driver_name(const struct fpal_device *dev);
const char *fpal_device_serial(const struct fpal_device *dev);
const char *fpal_device_fw_version(const struct fpal_device *dev);

/*
 * List template ids stored on the sensor. @cap is the capacity of @out,
 * the number of ids is returned in @n.
 */
int fpal_list(struct fpal_device *dev, struct fpal_template_id *out, size_t cap,
	     size_t *n);

/*
 * Wait up to @timeout_ms for a finger and identify it against every template
 * stored on the sensor. Returns FPAL_OK and fills @match on success,
 * FPAL_E_NOMATCH if the finger is unknown.
 */
int fpal_identify(struct fpal_device *dev, uint32_t timeout_ms,
		 struct fpal_template_id *match);

/*
 * Enrolling stores a NEW template on the sensor. It exists only when fpal is
 * built with "make ENROLL=1"; otherwise both functions return FPAL_E_NOTSUP.
 * No function of this library ever deletes a template.
 */

/* What fpal_enroll() reports while it runs. @stage counts accepted scans. */
enum fpal_enroll_event {
	FPAL_ENROLL_CHECK_TOUCH,  /* first touch: is this finger new? */
	FPAL_ENROLL_TOUCH,	  /* waiting for scan number @stage + 1 */
	FPAL_ENROLL_STAGE_DONE,	  /* a scan was accepted */
	FPAL_ENROLL_RETRY_CENTER, /* finger off centre, touch again */
	FPAL_ENROLL_RETRY_DIRTY,  /* sensor looks dirty, clean it */
	FPAL_ENROLL_RETRY,	  /* scan unreadable, lift and touch again */
};

typedef void (*fpal_enroll_cb)(enum fpal_enroll_event ev, uint8_t stage,
			       uint8_t total, void *ud);

/* Run every step but never send the final "store" command. */
#define FPAL_ENROLL_DRY_RUN (1u << 0)

/*
 * Enrol a finger under @new_id (see fpal_template_id_generate). Fails with
 * FPAL_E_FULL when the sensor holds the maximum number of templates, and
 * with FPAL_E_EXISTS when the id or the finger is already enrolled. Existing
 * templates are never modified.
 */
int fpal_enroll(struct fpal_device *dev, const struct fpal_template_id *new_id,
		uint32_t flags, fpal_enroll_cb cb, void *ud);

/* A fresh random id: FPAL_NEW_ID_PREFIX followed by hex digits. */
int fpal_template_id_generate(struct fpal_template_id *out);

/*
 * Abort a pending finger wait. Async-signal-safe: may be called from a
 * signal handler.
 */
void fpal_cancel(struct fpal_device *dev);

#endif /* FPAL_FPAL_H */
