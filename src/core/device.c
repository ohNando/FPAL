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

int fpal_device_open(const struct fpal_sensor_info *info,
		     struct fpal_device **out) {
	const struct fpal_usb_id *id;
	const struct fpal_driver *drv;
	struct fpal_transport tr;
	struct fpal_device *dev;
	int lock_fd, err;

	if (!info || !out)
		return FPAL_E_INVAL;
	if (info->bus != FPAL_BUS_USB)
		return FPAL_E_NOTSUP;

	drv = fpal_driver_lookup(info->vid, info->pid, &id);
	if (!drv)
		return FPAL_E_NODEV;

	lock_fd = fpal_lock_acquire();
	if (lock_fd < 0)
		return lock_fd;

	err = fpal_usbdevfs_open(info->busnum, info->devnum, &tr);
	if (err) {
		fpal_lock_release(lock_fd);
		return err;
	}

	err = fpal_device_open_transport(drv, id, tr, lock_fd, &dev);
	if (err)
		return err;

	/* sysfs already decoded the USB serial string descriptor for us. */
	if (!dev->serial[0])
		memcpy(dev->serial, info->serial, sizeof(dev->serial));

	*out = dev;
	return FPAL_OK;
}

int fpal_device_open_first(struct fpal_device **out) {
	struct fpal_sensor_info info;
	size_t n = 0;
	int err;

	err = fpal_scan(&info, 1, &n);
	if (err)
		return err;
	if (n == 0)
		return FPAL_E_NODEV;
	return fpal_device_open(&info, out);
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

size_t fpal_device_max_templates(const struct fpal_device *dev) {
	return dev->drv->max_templates;
}

size_t fpal_device_new_id_len(const struct fpal_device *dev) {
	return dev->drv->new_id_len;
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

#ifdef FPAL_ENABLE_ENROLL
static bool id_stored(const struct fpal_template_id *ids, size_t n,
		      const struct fpal_template_id *id) {
	for (size_t i = 0; i < n; i++)
		if (fpal_id_equal(&ids[i], id))
			return true;
	return false;
}

/* The sensor's capacity as far as the core can use it for array bounds. */
static size_t list_cap(const struct fpal_device *dev) {
	return dev->drv->max_templates < FPAL_MAX_TEMPLATES ?
		       dev->drv->max_templates :
		       FPAL_MAX_TEMPLATES;
}

/*
 * The safety rules of every write live here, not in the drivers, so a new
 * driver cannot forget them: look before touching anything, never go past
 * the capacity, never reuse an id, and trust the sensor's own list over a
 * status word afterwards.
 */
int fpal_enroll(struct fpal_device *dev, const struct fpal_template_id *new_id,
		uint32_t flags, fpal_enroll_cb cb, void *ud) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	size_t n = 0;
	int err;

	if (!dev || !new_id)
		return FPAL_E_INVAL;
	if (!dev->drv->enroll)
		return FPAL_E_NOTSUP;

	/* Read only: refuse before the sensor is put into enrol mode. */
	err = dev->drv->list(dev, ids, list_cap(dev), &n);
	if (err)
		return err;
	if (n >= dev->drv->max_templates)
		return FPAL_E_FULL;
	if (id_stored(ids, n, new_id))
		return FPAL_E_EXISTS;

	err = dev->drv->enroll(dev, new_id, ids, n, flags, cb, ud);
	if (err || (flags & FPAL_ENROLL_DRY_RUN))
		return err;

	err = dev->drv->list(dev, ids, list_cap(dev), &n);
	if (err)
		return err;
	return id_stored(ids, n, new_id) ? FPAL_OK : FPAL_E_PROTO;
}

int fpal_delete(struct fpal_device *dev, const struct fpal_template_id *id) {
	struct fpal_template_id before[FPAL_MAX_TEMPLATES];
	struct fpal_template_id after[FPAL_MAX_TEMPLATES];
	size_t n_before = 0, n_after = 0;
	int err;

	if (!dev || !id)
		return FPAL_E_INVAL;
	if (!dev->drv->delete_template)
		return FPAL_E_NOTSUP;

	err = dev->drv->list(dev, before, list_cap(dev), &n_before);
	if (err)
		return err;
	if (!id_stored(before, n_before, id))
		return FPAL_E_NODATA;

	err = dev->drv->delete_template(dev, id);
	if (err)
		return err;

	err = dev->drv->list(dev, after, list_cap(dev), &n_after);
	if (err)
		return err;
	/* exactly @id gone, everything else untouched */
	if (n_after != n_before - 1 || id_stored(after, n_after, id))
		return FPAL_E_PROTO;
	for (size_t i = 0; i < n_before; i++)
		if (!fpal_id_equal(&before[i], id) &&
		    !id_stored(after, n_after, &before[i]))
			return FPAL_E_PROTO;
	return FPAL_OK;
}
#else
/* Read-only build: nothing here can change what the sensor stores. */
int fpal_enroll(struct fpal_device *dev, const struct fpal_template_id *new_id,
		uint32_t flags, fpal_enroll_cb cb, void *ud) {
	(void)dev, (void)new_id, (void)flags, (void)cb, (void)ud;
	return FPAL_E_NOTSUP;
}

int fpal_delete(struct fpal_device *dev, const struct fpal_template_id *id) {
	(void)dev, (void)id;
	return FPAL_E_NOTSUP;
}
#endif

void fpal_cancel(struct fpal_device *dev) {
	if (dev)
		dev->tr.ops->cancel(dev->tr.ctx);
}
