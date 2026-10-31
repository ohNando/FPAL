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
 * Abort a pending finger wait. Async-signal-safe: may be called from a
 * signal handler.
 */
void fpal_cancel(struct fpal_device *dev);

#endif /* FPAL_FPAL_H */
