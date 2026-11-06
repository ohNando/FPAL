/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Fingerprint sensors that the firmware describes in ACPI instead of showing
 * them on USB. Written by hand from what libfprint (the elanspi driver) and
 * the community projects around the Goodix GXFP5130 report; "fpal report"
 * uses it to say what such a device is. A hardware id matches when it starts
 * with @prefix; the first matching line wins, so list longer prefixes first.
 */
#ifndef FPAL_TOOLS_KNOWN_ACPI_H
#define FPAL_TOOLS_KNOWN_ACPI_H

struct fpal_known_acpi {
	const char *prefix;  /* start of the ACPI hardware id */
	const char *vendor;
	const char *what;    /* one line for the report */
	const char *devnode; /* device node a driver for it would use, or NULL */
};

static const struct fpal_known_acpi fpal_known_acpi[] = {
	{ "GXFP5130", "Goodix",
	  "behind the Embedded Controller (eSPI mailbox); no mainline Linux driver, "
	  "a community kernel module offers /dev/gxfp",
	  "/dev/gxfp" },
	{ "GXFP", "Goodix", "SPI or eSPI sensor; no mainline Linux driver", NULL },
	{ "ELAN7001", "Elan", "SPI image sensor (libfprint driver elanspi)", NULL },
	{ "ELAN70A1", "Elan", "SPI image sensor (libfprint driver elanspi)", NULL },
};

#endif /* FPAL_TOOLS_KNOWN_ACPI_H */
