// SPDX-License-Identifier: Apache-2.0
/*
 * Driver registry and USB bus scan. Devices are discovered through sysfs
 * (/sys/bus/usb/devices/<port>/idVendor, idProduct, busnum, devnum, ...).
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver.h"

extern const struct fpx_driver fpx_driver_egis_moc;

const struct fpx_driver *const fpx_drivers[] = {
	&fpx_driver_egis_moc,
	NULL,
};

const struct fpx_driver *fpx_driver_lookup(uint16_t vid, uint16_t pid,
					   const struct fpx_usb_id **id)
{
	for (size_t d = 0; fpx_drivers[d]; d++) {
		for (const struct fpx_usb_id *e = fpx_drivers[d]->ids; e->vid;
		     e++) {
			if (e->vid == vid && e->pid == pid) {
				if (id)
					*id = e;
				return fpx_drivers[d];
			}
		}
	}
	return NULL;
}

/* Read a one-line sysfs attribute into @buf, stripping the newline. */
static bool read_attr(const char *dir, const char *name, char *buf,
		      size_t cap)
{
	char path[512];
	FILE *f;
	size_t len;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "re");
	if (!f)
		return false;
	if (!fgets(buf, (int)cap, f)) {
		fclose(f);
		return false;
	}
	fclose(f);

	len = strcspn(buf, "\n");
	buf[len] = '\0';
	return true;
}

static bool read_attr_ulong(const char *dir, const char *name, int base,
			    unsigned long max, unsigned long *out)
{
	char buf[32];
	char *end;

	if (!read_attr(dir, name, buf, sizeof(buf)))
		return false;
	*out = strtoul(buf, &end, base);
	return end != buf && *end == '\0' && *out <= max;
}

int fpx_scan(struct fpx_usb_info *out, size_t cap, size_t *n)
{
	DIR *d = opendir(FPX_SYSFS_USB_DEVICES);
	struct dirent *ent;
	size_t found = 0;

	if (!d)
		return FPX_E_IO;

	while ((ent = readdir(d))) {
		char dir[384];
		unsigned long vid, pid, bus, dev;
		const struct fpx_driver *drv;
		struct fpx_usb_info *info;

		/* Skip ".", ".." and interface entries such as "3-6:1.0". */
		if (ent->d_name[0] == '.' || strchr(ent->d_name, ':'))
			continue;

		snprintf(dir, sizeof(dir), "%s/%s", FPX_SYSFS_USB_DEVICES,
			 ent->d_name);

		if (!read_attr_ulong(dir, "idVendor", 16, UINT16_MAX, &vid) ||
		    !read_attr_ulong(dir, "idProduct", 16, UINT16_MAX, &pid))
			continue;

		drv = fpx_driver_lookup((uint16_t)vid, (uint16_t)pid, NULL);
		if (!drv)
			continue;

		if (!read_attr_ulong(dir, "busnum", 10, UINT8_MAX, &bus) ||
		    !read_attr_ulong(dir, "devnum", 10, UINT8_MAX, &dev))
			continue;

		if (found < cap) {
			info = &out[found];
			memset(info, 0, sizeof(*info));
			info->busnum = (uint8_t)bus;
			info->devnum = (uint8_t)dev;
			info->vid = (uint16_t)vid;
			info->pid = (uint16_t)pid;
			info->driver_name = drv->name;
			read_attr(dir, "product", info->product,
				  sizeof(info->product));
			read_attr(dir, "serial", info->serial,
				  sizeof(info->serial));
		}
		found++;
	}

	closedir(d);
	*n = found < cap ? found : cap;
	return FPX_OK;
}
