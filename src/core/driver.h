/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Driver interface. Every sensor family implements one struct fpal_driver and
 * is listed in the registry (src/core/registry.c). Adding a sensor never
 * requires touching the core.
 *
 * The only operations that change what the sensor stores are enroll and
 * delete_template. Both are NULL unless the build defines FPAL_ENABLE_ENROLL
 * (left out by "make ENROLL=0"). delete_template removes exactly one
 * template; no driver operation clears the sensor.
 */
#ifndef FPAL_CORE_DRIVER_H
#define FPAL_CORE_DRIVER_H

#include "fpal/fpal.h"
#include "transport/transport.h"

struct fpal_device {
	const struct fpal_driver *drv;
	const struct fpal_usb_id *id;
	struct fpal_transport tr;
	int lock_fd;
	char serial[FPAL_STR_MAX];
	char fw_version[FPAL_STR_MAX];
	void *priv; /* drv->priv_size bytes, zero initialised */
};

struct fpal_driver {
	const char *name;
	const struct fpal_usb_id *ids; /* terminated by vid == 0 */
	uint32_t caps;		      /* FPAL_CAP_* */
	size_t priv_size;

	int (*open)(struct fpal_device *dev);
	void (*close)(struct fpal_device *dev);
	int (*list)(struct fpal_device *dev, struct fpal_template_id *out,
		    size_t cap, size_t *n);
	int (*identify)(struct fpal_device *dev, uint32_t timeout_ms,
			struct fpal_template_id *match);
	/* Optional, NULL in read-only builds. */
	int (*enroll)(struct fpal_device *dev,
		      const struct fpal_template_id *new_id, uint32_t flags,
		      fpal_enroll_cb cb, void *ud);
	/* Optional, NULL in read-only builds. Deletes ONE template. */
	int (*delete_template)(struct fpal_device *dev,
			       const struct fpal_template_id *id);
};

/* NULL terminated table of all built-in drivers. */
extern const struct fpal_driver *const fpal_drivers[];

/* Find the driver and id entry matching @vid:@pid. */
const struct fpal_driver *fpal_driver_lookup(uint16_t vid, uint16_t pid,
					   const struct fpal_usb_id **id);

/*
 * Create a device on top of an already opened transport and run the
 * driver's open(). Takes ownership of @tr. Used by the public open path and
 * by tests with a mock transport.
 */
int fpal_device_open_transport(const struct fpal_driver *drv,
			      const struct fpal_usb_id *id,
			      struct fpal_transport tr, int lock_fd,
			      struct fpal_device **out);

#endif /* FPAL_CORE_DRIVER_H */
