/* SPDX-License-Identifier: Apache-2.0 */
/*
 * fpx public API.
 */
#ifndef FPX_FPX_H
#define FPX_FPX_H

#include "config.h"
#include "types.h"

struct fpx_device;

/* Location and identity of a supported sensor found on the USB bus. */
struct fpx_usb_info {
	uint8_t busnum;
	uint8_t devnum;
	uint16_t vid;
	uint16_t pid;
	char product[FPX_STR_MAX];
	char serial[FPX_STR_MAX];
	const char *driver_name;
};

/* Human readable message for an fpx_err value. */
const char *fpx_strerror(int err);

/*
 * Scan the USB bus for supported sensors. Fills up to @cap entries of @out
 * and stores the number found in @n. Does not open the devices.
 */
int fpx_scan(struct fpx_usb_info *out, size_t cap, size_t *n);

/* Open the first supported sensor. Requires root. */
int fpx_device_open_first(struct fpx_device **out);

void fpx_device_close(struct fpx_device *dev);

const char *fpx_device_driver_name(const struct fpx_device *dev);
const char *fpx_device_serial(const struct fpx_device *dev);
const char *fpx_device_fw_version(const struct fpx_device *dev);

/*
 * List template ids stored on the sensor. @cap is the capacity of @out,
 * the number of ids is returned in @n.
 */
int fpx_list(struct fpx_device *dev, struct fpx_template_id *out, size_t cap,
	     size_t *n);

/*
 * Wait up to @timeout_ms for a finger and identify it against every template
 * stored on the sensor. Returns FPX_OK and fills @match on success,
 * FPX_E_NOMATCH if the finger is unknown.
 */
int fpx_identify(struct fpx_device *dev, uint32_t timeout_ms,
		 struct fpx_template_id *match);

/*
 * Abort a pending finger wait. Async-signal-safe: may be called from a
 * signal handler.
 */
void fpx_cancel(struct fpx_device *dev);

#endif /* FPX_FPX_H */
