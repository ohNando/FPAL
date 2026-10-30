/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Driver interface. Every sensor family implements one struct fpx_driver and
 * is listed in the registry (src/core/registry.c). Adding a sensor never
 * requires touching the core.
 *
 * Deliberately there is no delete operation: templates stored on the sensor
 * (for example by Windows Hello) are never removed by fpx.
 */
#ifndef FPX_CORE_DRIVER_H
#define FPX_CORE_DRIVER_H

#include "fpx/fpx.h"
#include "transport/transport.h"

typedef void (*fpx_progress_cb)(uint8_t stage, uint8_t total, void *ud);

struct fpx_device {
	const struct fpx_driver *drv;
	const struct fpx_usb_id *id;
	struct fpx_transport tr;
	int lock_fd;
	char serial[FPX_STR_MAX];
	char fw_version[FPX_STR_MAX];
	void *priv; /* drv->priv_size bytes, zero initialised */
};

struct fpx_driver {
	const char *name;
	const struct fpx_usb_id *ids; /* terminated by vid == 0 */
	uint32_t caps;		      /* FPX_CAP_* */
	size_t priv_size;

	int (*open)(struct fpx_device *dev);
	void (*close)(struct fpx_device *dev);
	int (*list)(struct fpx_device *dev, struct fpx_template_id *out,
		    size_t cap, size_t *n);
	int (*identify)(struct fpx_device *dev, uint32_t timeout_ms,
			struct fpx_template_id *match);
	/* Optional, NULL unless built with ENROLL=1. */
	int (*enroll)(struct fpx_device *dev,
		      const struct fpx_template_id *new_id, fpx_progress_cb cb,
		      void *ud);
};

/* NULL terminated table of all built-in drivers. */
extern const struct fpx_driver *const fpx_drivers[];

/* Find the driver and id entry matching @vid:@pid. */
const struct fpx_driver *fpx_driver_lookup(uint16_t vid, uint16_t pid,
					   const struct fpx_usb_id **id);

/*
 * Create a device on top of an already opened transport and run the
 * driver's open(). Takes ownership of @tr. Used by the public open path and
 * by tests with a mock transport.
 */
int fpx_device_open_transport(const struct fpx_driver *drv,
			      const struct fpx_usb_id *id,
			      struct fpx_transport tr, int lock_fd,
			      struct fpx_device **out);

#endif /* FPX_CORE_DRIVER_H */
