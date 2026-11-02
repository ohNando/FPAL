/* SPDX-License-Identifier: Apache-2.0 */
/*
 * fpal shared types.
 */
#ifndef FPAL_TYPES_H
#define FPAL_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

/*
 * Error codes. Functions return 0 (FPAL_OK) on success and one of these
 * negative values on failure.
 */
enum fpal_err {
	FPAL_OK = 0,
	FPAL_E_INVAL = -1,     /* invalid argument */
	FPAL_E_NOMEM = -2,     /* out of memory */
	FPAL_E_NODEV = -3,     /* no supported sensor found */
	FPAL_E_PERM = -4,      /* permission denied (run as root) */
	FPAL_E_BUSY = -5,      /* sensor in use by another process */
	FPAL_E_IO = -6,        /* USB transfer failed */
	FPAL_E_TIMEOUT = -7,   /* timed out */
	FPAL_E_CANCELLED = -8, /* cancelled by the caller */
	FPAL_E_PROTO = -9,     /* unexpected response from the sensor */
	FPAL_E_NOMATCH = -10,  /* finger did not match any template */
	FPAL_E_NODATA = -11,   /* no templates stored on the sensor */
	FPAL_E_NOTSUP = -12,   /* operation not supported by this driver */
	FPAL_E_OVERFLOW = -13, /* buffer too small */
	FPAL_E_FULL = -14,     /* sensor storage is full */
	FPAL_E_EXISTS = -15,   /* finger or id is already enrolled */
	FPAL_E_DROPPED = -16,  /* a message was too big or malformed and was skipped */
};

/* Driver capability flags (struct fpal_driver.caps). */
#define FPAL_CAP_MOC (1u << 0)    /* match-on-chip: templates stored on sensor */
#define FPAL_CAP_IMAGE (1u << 1)  /* sensor delivers raw images */
#define FPAL_CAP_ENROLL (1u << 2) /* driver supports enrolling new prints */

/* Opaque identifier of a template stored on the sensor. */
struct fpal_template_id {
	uint8_t bytes[FPAL_TEMPLATE_ID_LEN];
};

/* One entry of a driver's USB id table; terminated by vid == 0. */
struct fpal_usb_id {
	uint16_t vid;
	uint16_t pid;
	uint32_t flags; /* driver private quirk flags */
};

#endif /* FPAL_TYPES_H */
