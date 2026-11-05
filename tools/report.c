// SPDX-License-Identifier: Apache-2.0
#include "report.h"

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#include "core/driver.h"
#include "fpal/fpal.h"
#include "known_sensors.h"

static bool read_attr(const char *dir, const char *name, char *buf, size_t cap) {
	char path[512];
	FILE *f;
	size_t n;

	if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir, name) >= sizeof(path))
		return false;
	f = fopen(path, "r");
	if (!f)
		return false;
	n = fread(buf, 1, cap - 1, f);
	fclose(f);
	buf[n] = '\0';
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
		buf[--n] = '\0';
	return true;
}

static unsigned long attr_num(const char *dir, const char *name, int base,
			      unsigned long max) {
	char buf[32], *end;
	unsigned long v;

	if (!read_attr(dir, name, buf, sizeof(buf)) || !buf[0])
		return 0;
	v = strtoul(buf, &end, base);
	return (*end == '\0' && v <= max) ? v : 0;
}

static void read_endpoint(const char *dir, struct fpal_report_ep *ep) {
	ep->address = (uint8_t)attr_num(dir, "bEndpointAddress", 16, 0xff);
	ep->max_packet = (uint16_t)attr_num(dir, "wMaxPacketSize", 16, 0xffff);
	if (!read_attr(dir, "type", ep->type, sizeof(ep->type)))
		ep->type[0] = '\0';
	fpal_text_sanitize(ep->type);
}

static void read_interface(const char *dir, struct fpal_report_iface *ifc) {
	DIR *d = opendir(dir);
	struct dirent *ent;

	ifc->number = (uint8_t)attr_num(dir, "bInterfaceNumber", 16, 0xff);
	ifc->cls = (uint8_t)attr_num(dir, "bInterfaceClass", 16, 0xff);
	ifc->subcls = (uint8_t)attr_num(dir, "bInterfaceSubClass", 16, 0xff);
	ifc->proto = (uint8_t)attr_num(dir, "bInterfaceProtocol", 16, 0xff);
	if (!d)
		return;
	while ((ent = readdir(d)) && ifc->n_ep < FPAL_REPORT_EPS) {
		char path[512];

		if (strncmp(ent->d_name, "ep_", 3) != 0)
			continue;
		if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name) >=
		    sizeof(path))
			continue;
		read_endpoint(path, &ifc->ep[ifc->n_ep++]);
	}
	closedir(d);
}

/* Keep the endpoints in a stable order whatever order readdir used. */
static int ep_cmp(const void *a, const void *b) {
	return (int)((const struct fpal_report_ep *)a)->address -
	       (int)((const struct fpal_report_ep *)b)->address;
}

static int iface_cmp(const void *a, const void *b) {
	return (int)((const struct fpal_report_iface *)a)->number -
	       (int)((const struct fpal_report_iface *)b)->number;
}

static const struct fpal_known_sensor *known(uint16_t vid, uint16_t pid) {
	for (size_t i = 0; i < sizeof(fpal_known_sensors) / sizeof(fpal_known_sensors[0]); i++)
		if (fpal_known_sensors[i].vid == vid && fpal_known_sensors[i].pid == pid)
			return &fpal_known_sensors[i];
	return NULL;
}

static bool name_says_fingerprint(const char *s) {
	static const char *const words[] = { "finger", "biometric", "fprint" };

	for (size_t w = 0; w < sizeof(words) / sizeof(words[0]); w++) {
		size_t len = strlen(words[w]);

		for (const char *p = s; *p; p++)
			if (strncasecmp(p, words[w], len) == 0)
				return true;
	}
	return false;
}

static int dev_cmp(const void *a, const void *b) {
	return strcmp(((const struct fpal_report_dev *)a)->port,
		      ((const struct fpal_report_dev *)b)->port);
}

int fpal_report_scan(const char *sysfs_dir, bool all, struct fpal_report_dev *out,
		     size_t cap, size_t *n) {
	DIR *d;
	struct dirent *ent;
	size_t found = 0;

	if (!sysfs_dir || !out || !n)
		return FPAL_E_INVAL;
	d = opendir(sysfs_dir);
	if (!d)
		return FPAL_E_IO;

	while ((ent = readdir(d)) && found < cap) {
		struct fpal_report_dev *dev = &out[found];
		const struct fpal_known_sensor *k;
		char dir[512], sub[512];
		bool vendor_specific = false;
		DIR *dd;
		struct dirent *e2;
		size_t plen;

		/* Devices only: not ".", ".." or interfaces such as "3-6:1.0". */
		if (ent->d_name[0] == '.' || strchr(ent->d_name, ':') ||
		    strlen(ent->d_name) >= sizeof(dev->port))
			continue;
		if ((size_t)snprintf(dir, sizeof(dir), "%s/%s", sysfs_dir,
				     ent->d_name) >= sizeof(dir))
			continue;
		memset(dev, 0, sizeof(*dev));
		dev->vid = (uint16_t)attr_num(dir, "idVendor", 16, 0xffff);
		dev->pid = (uint16_t)attr_num(dir, "idProduct", 16, 0xffff);
		if (!dev->vid)
			continue;
		snprintf(dev->port, sizeof(dev->port), "%s", ent->d_name);
		dev->bcd = (uint16_t)attr_num(dir, "bcdDevice", 16, 0xffff);
		dev->speed = (unsigned)attr_num(dir, "speed", 10, 100000);
		dev->dev_class = (uint8_t)attr_num(dir, "bDeviceClass", 16, 0xff);
		read_attr(dir, "manufacturer", dev->manufacturer, sizeof(dev->manufacturer));
		read_attr(dir, "product", dev->product, sizeof(dev->product));
		/* a device chooses these strings; they end up on terminals */
		fpal_text_sanitize(dev->manufacturer);
		fpal_text_sanitize(dev->product);

		/* Interfaces are the sub-directories named "<port>:<cfg>.<n>". */
		plen = strlen(ent->d_name);
		dd = opendir(dir);
		while (dd && (e2 = readdir(dd)) && dev->n_iface < FPAL_REPORT_IFACES) {
			if (strncmp(e2->d_name, ent->d_name, plen) != 0 ||
			    e2->d_name[plen] != ':')
				continue;
			if ((size_t)snprintf(sub, sizeof(sub), "%s/%s", dir, e2->d_name) >=
			    sizeof(sub))
				continue;
			read_interface(sub, &dev->iface[dev->n_iface]);
			if (dev->iface[dev->n_iface].cls == 0xff)
				vendor_specific = true;
			dev->n_iface++;
		}
		if (dd)
			closedir(dd);
		qsort(dev->iface, dev->n_iface, sizeof(dev->iface[0]), iface_cmp);
		for (size_t i = 0; i < dev->n_iface; i++)
			qsort(dev->iface[i].ep, dev->iface[i].n_ep,
			      sizeof(dev->iface[i].ep[0]), ep_cmp);

		k = known(dev->vid, dev->pid);
		if (k) {
			dev->libfprint = k->libfprint;
			dev->moc = k->moc;
		}
		{
			const struct fpal_driver *drv =
				fpal_driver_lookup(dev->vid, dev->pid, NULL);

			dev->fpal = drv ? drv->name : NULL;
		}

		if (!all && !k && !vendor_specific &&
		    !name_says_fingerprint(dev->product) &&
		    !name_says_fingerprint(dev->manufacturer))
			continue;
		found++;
	}
	closedir(d);
	qsort(out, found, sizeof(out[0]), dev_cmp);
	*n = found;
	return FPAL_OK;
}

static const char *ep_dir(uint8_t addr) {
	return (addr & 0x80) ? "IN " : "OUT";
}

void fpal_report_print(FILE *f, const struct fpal_report_dev *devs, size_t n,
		       bool markdown) {
	struct utsname un;

	if (markdown)
		fputs("```\n", f);
	fputs("fpal report " FPAL_VERSION, f);
	if (uname(&un) == 0)
		fprintf(f, "   kernel %s", un.release);
	fputs("\n(read from sysfs; no serial numbers, nothing was sent to the device)\n", f);
	fputs("known ids: " FPAL_KNOWN_SENSORS_SOURCE "\n", f);

	if (n == 0)
		fputs("\nno USB fingerprint sensor candidates found\n", f);
	for (size_t i = 0; i < n; i++) {
		const struct fpal_report_dev *d = &devs[i];

		fprintf(f, "\ndevice %s: %04x:%04x  %s%s%s\n", d->port, d->vid, d->pid,
			d->product[0] ? d->product : "(no product string)",
			d->manufacturer[0] ? "  by " : "", d->manufacturer);
		if (d->libfprint)
			fprintf(f, "  libfprint: driver %s, %s\n", d->libfprint,
				d->moc ? "Match-on-Chip (templates live on the chip)" :
					 "image sensor (matching on the host)");
		else
			fputs("  libfprint: this id is not known to it\n", f);
		if (d->fpal)
			fprintf(f, "  fpal:      supported, driver %s\n", d->fpal);
		else if (d->libfprint && d->moc)
			fputs("  fpal:      not supported yet; a Match-on-Chip driver is "
			      "possible (see docs/adding-a-sensor.md)\n", f);
		else if (d->libfprint)
			fputs("  fpal:      not supported; image sensors are out of scope\n", f);
		else
			fputs("  fpal:      not supported\n", f);
		fprintf(f, "  usb:       bcdDevice %04x, class %02x, ", d->bcd, d->dev_class);
		if (d->speed)
			fprintf(f, "%u Mb/s, ", d->speed);
		fprintf(f, "%zu interface(s)\n", d->n_iface);
		for (size_t k = 0; k < d->n_iface; k++) {
			const struct fpal_report_iface *ifc = &d->iface[k];

			fprintf(f, "  interface %u: class %02x/%02x/%02x\n", ifc->number,
				ifc->cls, ifc->subcls, ifc->proto);
			for (size_t e = 0; e < ifc->n_ep; e++)
				fprintf(f, "    endpoint 0x%02x %s %-9s max packet %u\n",
					ifc->ep[e].address, ep_dir(ifc->ep[e].address),
					ifc->ep[e].type, ifc->ep[e].max_packet);
		}
	}
	if (markdown)
		fputs("```\n", f);
}

int fpal_cmd_report(int argc, char **argv) {
	struct fpal_report_dev devs[FPAL_REPORT_DEVS];
	bool markdown = false, all = false;
	size_t n = 0;
	int err;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--markdown"))
			markdown = true;
		else if (!strcmp(argv[i], "--all"))
			all = true;
		else {
			fputs("usage: fpal report [--markdown] [--all]\n", stderr);
			return 2;
		}
	}
	err = fpal_report_scan(FPAL_SYSFS_USB_DEVICES, all, devs,
			       FPAL_REPORT_DEVS, &n);
	if (err) {
		fprintf(stderr, "fpal: cannot read %s: %s\n", FPAL_SYSFS_USB_DEVICES,
			fpal_strerror(err));
		return 1;
	}
	fpal_report_print(stdout, devs, n, markdown);
	return 0;
}
