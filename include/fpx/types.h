/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * fpx shared types.
 */
#ifndef FPX_TYPES_H
#define FPX_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

/*
 * Error codes. Functions return 0 (FPX_OK) on success and one of these
 * negative values on failure.
 */
enum fpx_err {
	FPX_OK = 0,
	FPX_E_INVAL = -1,     /* invalid argument */
	FPX_E_NOMEM = -2,     /* out of memory */
	FPX_E_NODEV = -3,     /* no supported sensor found */
	FPX_E_PERM = -4,      /* permission denied (run as root) */
	FPX_E_BUSY = -5,      /* sensor in use by another process */
	FPX_E_IO = -6,        /* USB transfer failed */
	FPX_E_TIMEOUT = -7,   /* timed out */
	FPX_E_CANCELLED = -8, /* cancelled by the caller */
	FPX_E_PROTO = -9,     /* unexpected response from the sensor */
	FPX_E_NOMATCH = -10,  /* finger did not match any template */
	FPX_E_NODATA = -11,   /* no templates stored on the sensor */
	FPX_E_NOTSUP = -12,   /* operation not supported by this driver */
	FPX_E_OVERFLOW = -13, /* buffer too small */
};

/* Driver capability flags (struct fpx_driver.caps). */
#define FPX_CAP_MOC (1u << 0)    /* match-on-chip: templates stored on sensor */
#define FPX_CAP_IMAGE (1u << 1)  /* sensor delivers raw images */
#define FPX_CAP_ENROLL (1u << 2) /* driver supports enrolling new prints */

/* Opaque identifier of a template stored on the sensor. */
struct fpx_template_id {
	uint8_t bytes[FPX_TEMPLATE_ID_LEN];
};

/* One entry of a driver's USB id table; terminated by vid == 0. */
struct fpx_usb_id {
	uint16_t vid;
	uint16_t pid;
	uint32_t flags; /* driver private quirk flags */
};

#endif /* FPX_TYPES_H */
