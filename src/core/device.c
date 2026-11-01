// SPDX-License-Identifier: Apache-2.0
/*
 * Public device API: thin dispatch layer between callers and drivers.
 */
#include <stdlib.h>
#include <string.h>

#include "driver.h"
#include "lock.h"
#include "log.h"

int fpal_device_open_transport(const struct fpal_driver *drv,
			      const struct fpal_usb_id *id,
			      struct fpal_transport tr, int lock_fd,
			      struct fpal_device **out) {
	struct fpal_device *dev = calloc(1, sizeof(*dev));
	int err;

	if (!dev)
		goto err_nomem;

	if (drv->priv_size) {
		dev->priv = calloc(1, drv->priv_size);
		if (!dev->priv) {
			free(dev);
			goto err_nomem;
		}
	}

	dev->drv = drv;
	dev->id = id;
	dev->tr = tr;
	dev->lock_fd = lock_fd;

	err = drv->open(dev);
	if (err) {
		tr.ops->close(tr.ctx);
		fpal_lock_release(lock_fd);
		free(dev->priv);
		free(dev);
		return err;
	}

	*out = dev;
	return FPAL_OK;

err_nomem:
	tr.ops->close(tr.ctx);
	fpal_lock_release(lock_fd);
	return FPAL_E_NOMEM;
}

int fpal_device_open_first(struct fpal_device **out) {
	struct fpal_usb_info info;
	const struct fpal_usb_id *id;
	const struct fpal_driver *drv;
	struct fpal_transport tr;
	struct fpal_device *dev;
	size_t n = 0;
	int lock_fd, err;

	err = fpal_scan(&info, 1, &n);
	if (err)
		return err;
	if (n == 0)
		return FPAL_E_NODEV;

	drv = fpal_driver_lookup(info.vid, info.pid, &id);
	if (!drv)
		return FPAL_E_NODEV;

	lock_fd = fpal_lock_acquire();
	if (lock_fd < 0)
		return lock_fd;

	err = fpal_usbdevfs_open(info.busnum, info.devnum, &tr);
	if (err) {
		fpal_lock_release(lock_fd);
		return err;
	}

	err = fpal_device_open_transport(drv, id, tr, lock_fd, &dev);
	if (err)
		return err;

	/* sysfs already decoded the USB serial string descriptor for us. */
	if (!dev->serial[0])
		memcpy(dev->serial, info.serial, sizeof(dev->serial));

	*out = dev;
	return FPAL_OK;
}

void fpal_device_close(struct fpal_device *dev) {
	if (!dev)
		return;
	dev->drv->close(dev);
	dev->tr.ops->close(dev->tr.ctx);
	fpal_lock_release(dev->lock_fd);
	free(dev->priv);
	free(dev);
}

const char *fpal_device_driver_name(const struct fpal_device *dev) {
	return dev->drv->name;
}

const char *fpal_device_serial(const struct fpal_device *dev) {
	return dev->serial;
}

const char *fpal_device_fw_version(const struct fpal_device *dev) {
	return dev->fw_version;
}

int fpal_list(struct fpal_device *dev, struct fpal_template_id *out, size_t cap,
	     size_t *n) {
	if (!dev || !out || !n)
		return FPAL_E_INVAL;
	return dev->drv->list(dev, out, cap, n);
}

int fpal_identify(struct fpal_device *dev, uint32_t timeout_ms,
		 struct fpal_template_id *match) {
	if (!dev || !match)
		return FPAL_E_INVAL;
	return dev->drv->identify(dev, timeout_ms, match);
}

int fpal_enroll(struct fpal_device *dev, const struct fpal_template_id *new_id,
		uint32_t flags, fpal_enroll_cb cb, void *ud) {
	if (!dev || !new_id)
		return FPAL_E_INVAL;
	if (!dev->drv->enroll)
		return FPAL_E_NOTSUP;
	return dev->drv->enroll(dev, new_id, flags, cb, ud);
}

void fpal_cancel(struct fpal_device *dev) {
	if (dev)
		dev->tr.ops->cancel(dev->tr.ctx);
}
