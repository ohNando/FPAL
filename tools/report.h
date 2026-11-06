/* SPDX-License-Identifier: Apache-2.0 */
/*
 * "fpal report": what a fingerprint sensor looks like to the kernel, in a
 * form that can be pasted into an issue. It reads /sys only, needs no root
 * and sends nothing to the device. The serial number is left out on purpose,
 * the report is meant to be public.
 */
#ifndef FPAL_TOOLS_REPORT_H
#define FPAL_TOOLS_REPORT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define FPAL_REPORT_DEVS 8u
#define FPAL_REPORT_IFACES 8u
#define FPAL_REPORT_EPS 16u

struct fpal_report_ep {
	uint8_t address;
	char type[16]; /* "Bulk", "Interrupt", ... as sysfs names it */
	uint16_t max_packet;
};

struct fpal_report_iface {
	uint8_t number, cls, subcls, proto;
	struct fpal_report_ep ep[FPAL_REPORT_EPS];
	size_t n_ep;
};

struct fpal_report_dev {
	char port[32]; /* sysfs name, e.g. "3-6" */
	uint16_t vid, pid, bcd;
	char manufacturer[64], product[64];
	unsigned speed; /* Mb/s, 0 if unknown */
	uint8_t dev_class;
	struct fpal_report_iface iface[FPAL_REPORT_IFACES];
	size_t n_iface;

	/* What we know about the id. */
	const char *libfprint; /* libfprint driver, or NULL if unknown to it */
	bool moc;	       /* Match-on-Chip according to libfprint */
	const char *fpal;      /* our driver, or NULL */
};

#define FPAL_REPORT_ACPI 8u

/* A fingerprint sensor that ACPI describes (it is not on USB). */
struct fpal_report_acpi {
	char name[32];	   /* sysfs name, e.g. "GXFP5130:00" */
	char hid[32];	   /* ACPI hardware id, e.g. "GXFP5130" */
	char path[128];	   /* ACPI namespace path */
	int status;	   /* ACPI _STA bits (1 present, 2 enabled), -1 unknown */
	char driver[32];   /* kernel driver bound to it, "" if none */
	const char *vendor;  /* from tools/known_acpi.h */
	const char *what;
	const char *devnode; /* device node a driver would use, or NULL */
	bool devnode_present;
};

/*
 * Walk @sysfs_dir (normally /sys/bus/usb/devices). Without @all only
 * sensors libfprint knows, devices called "fingerprint" or similar and
 * devices with a vendor-specific interface are kept. Returns the number
 * stored in @out (at most @cap), or a negative fpal_err.
 */
int fpal_report_scan(const char *sysfs_dir, bool all, struct fpal_report_dev *out,
		     size_t cap, size_t *n);

/*
 * Walk @acpi_dir (normally /sys/bus/acpi/devices) for sensors that ACPI
 * describes. @root is put in front of device node paths ("" on a real
 * system). A missing @acpi_dir (no ACPI) is not an error: *@n is 0.
 */
int fpal_report_scan_acpi(const char *acpi_dir, const char *root,
			  struct fpal_report_acpi *out, size_t cap, size_t *n);

/* Write the report. @markdown fences it so it survives pasting into GitHub. */
void fpal_report_print(FILE *f, const struct fpal_report_dev *devs, size_t n,
		       const struct fpal_report_acpi *acpi, size_t n_acpi,
		       bool markdown);

int fpal_cmd_report(int argc, char **argv);

#endif /* FPAL_TOOLS_REPORT_H */
