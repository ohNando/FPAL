// SPDX-License-Identifier: Apache-2.0
/*
 * Public device API: thin dispatch layer between callers and drivers.
 */
#include <stdlib.h>
#include <string.h>

#include "driver.h"
#include "lock.h"
#include "log.h"

int fpx_device_open_transport(const struct fpx_driver *drv,
			      const struct fpx_usb_id *id,
			      struct fpx_transport tr, int lock_fd,
			      struct fpx_device **out)
{
	struct fpx_device *dev = calloc(1, sizeof(*dev));
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
		fpx_lock_release(lock_fd);
		free(dev->priv);
		free(dev);
		return err;
	}

	*out = dev;
	return FPX_OK;

err_nomem:
	tr.ops->close(tr.ctx);
	fpx_lock_release(lock_fd);
	return FPX_E_NOMEM;
}

int fpx_device_open_first(struct fpx_device **out)
{
	struct fpx_usb_info info;
	const struct fpx_usb_id *id;
	const struct fpx_driver *drv;
	struct fpx_transport tr;
	struct fpx_device *dev;
	size_t n = 0;
	int lock_fd, err;

	err = fpx_scan(&info, 1, &n);
	if (err)
		return err;
	if (n == 0)
		return FPX_E_NODEV;

	drv = fpx_driver_lookup(info.vid, info.pid, &id);
	if (!drv)
		return FPX_E_NODEV;

	lock_fd = fpx_lock_acquire();
	if (lock_fd < 0)
		return lock_fd;

	err = fpx_usbdevfs_open(info.busnum, info.devnum, &tr);
	if (err) {
		fpx_lock_release(lock_fd);
		return err;
	}

	err = fpx_device_open_transport(drv, id, tr, lock_fd, &dev);
	if (err)
		return err;

	/* sysfs already decoded the USB serial string descriptor for us. */
	if (!dev->serial[0])
		memcpy(dev->serial, info.serial, sizeof(dev->serial));

	*out = dev;
	return FPX_OK;
}

void fpx_device_close(struct fpx_device *dev)
{
	if (!dev)
		return;
	dev->drv->close(dev);
	dev->tr.ops->close(dev->tr.ctx);
	fpx_lock_release(dev->lock_fd);
	free(dev->priv);
	free(dev);
}

const char *fpx_device_driver_name(const struct fpx_device *dev)
{
	return dev->drv->name;
}

const char *fpx_device_serial(const struct fpx_device *dev)
{
	return dev->serial;
}

const char *fpx_device_fw_version(const struct fpx_device *dev)
{
	return dev->fw_version;
}

int fpx_list(struct fpx_device *dev, struct fpx_template_id *out, size_t cap,
	     size_t *n)
{
	if (!dev || !out || !n)
		return FPX_E_INVAL;
	return dev->drv->list(dev, out, cap, n);
}

int fpx_identify(struct fpx_device *dev, uint32_t timeout_ms,
		 struct fpx_template_id *match)
{
	if (!dev || !match)
		return FPX_E_INVAL;
	return dev->drv->identify(dev, timeout_ms, match);
}

void fpx_cancel(struct fpx_device *dev)
{
	if (dev)
		dev->tr.ops->cancel(dev->tr.ctx);
}
