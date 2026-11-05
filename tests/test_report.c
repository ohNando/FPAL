// SPDX-License-Identifier: Apache-2.0
/*
 * "fpal report" on a made-up /sys/bus/usb/devices tree.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../tools/report.h"
#include "fpal/types.h"
#include "t.h"

static char root[64];

static void put(const char *rel, const char *text) {
	char path[640], dir[512], *slash;
	FILE *f;

	snprintf(dir, sizeof(dir), "%s/%s", root, rel);
	slash = strrchr(dir, '/');
	*slash = '\0';
	snprintf(path, sizeof(path), "/bin/mkdir -p '%s'", dir);
	if (system(path) != 0)
		return;
	snprintf(path, sizeof(path), "%s/%s", root, rel);
	f = fopen(path, "w");
	if (f) {
		fputs(text, f);
		fclose(f);
	}
}

static void device(const char *port, const char *vid, const char *pid,
		   const char *product, const char *cls) {
	char rel[128];

	snprintf(rel, sizeof(rel), "%s/idVendor", port);
	put(rel, vid);
	snprintf(rel, sizeof(rel), "%s/idProduct", port);
	put(rel, pid);
	snprintf(rel, sizeof(rel), "%s/product", port);
	put(rel, product);
	snprintf(rel, sizeof(rel), "%s/bDeviceClass", port);
	put(rel, cls);
}

static void iface(const char *port, const char *cls) {
	char rel[128];

	snprintf(rel, sizeof(rel), "%s/%s:1.0/bInterfaceNumber", port, port);
	put(rel, "00\n");
	snprintf(rel, sizeof(rel), "%s/%s:1.0/bInterfaceClass", port, port);
	put(rel, cls);
}

static void endpoint(const char *port, const char *ep, const char *type,
		     const char *max) {
	char rel[160];

	snprintf(rel, sizeof(rel), "%s/%s:1.0/ep_%s/bEndpointAddress", port, port, ep);
	put(rel, ep);
	snprintf(rel, sizeof(rel), "%s/%s:1.0/ep_%s/type", port, port, ep);
	put(rel, type);
	snprintf(rel, sizeof(rel), "%s/%s:1.0/ep_%s/wMaxPacketSize", port, port, ep);
	put(rel, max);
}

static void build_tree(void) {
	/* the Egis sensor, with a serial number that must not leak */
	device("3-6", "1c7a\n", "0587\n", "ETU905A88-E\n", "ff\n");
	put("3-6/manufacturer", "EGIS\n");
	put("3-6/serial", "SECRET-SERIAL-123\n");
	put("3-6/speed", "480\n");
	put("3-6/bcdDevice", "0001\n");
	iface("3-6", "ff\n");
	endpoint("3-6", "81", "Bulk\n", "0200\n");
	endpoint("3-6", "02", "Bulk\n", "0200\n");
	endpoint("3-6", "83", "Interrupt\n", "0040\n");
	/* the interface also shows up at the top level, as in real sysfs */
	put("3-6:1.0/bInterfaceClass", "ff\n");
	/* a hub: not interesting */
	device("1-1", "1d6b\n", "0002\n", "xHCI Host Controller\n", "09\n");
	iface("1-1", "09\n");
	/* a vendor-specific device libfprint has never heard of */
	device("2-3", "dead\n", "beef\n", "Mystery\n", "00\n");
	iface("2-3", "ff\n");
	/* called "fingerprint", but looks like an ordinary device */
	device("2-4", "beee\n", "0001\n", "Acme Fingerprint Reader\n", "00\n");
	iface("2-4", "03\n");
	/* a keyboard whose name tries to rewrite the screen */
	device("2-5", "0c45\n", "7000\n", "Key\x1b[2Jboard\n", "00\n");
	iface("2-5", "03\n");
}

static char *render(bool all, bool markdown, size_t cap) {
	struct fpal_report_dev devs[FPAL_REPORT_DEVS];
	size_t n = 0, size = 0;
	char *buf = NULL;
	FILE *f;

	if (fpal_report_scan(root, all, devs, cap, &n) != FPAL_OK)
		return NULL;
	f = open_memstream(&buf, &size);
	fpal_report_print(f, devs, n, markdown);
	fclose(f);
	return buf;
}

static void test_a_known_sensor_is_described(void) {
	char *out = render(false, false, FPAL_REPORT_DEVS);

	T_CHECK(out != NULL);
	if (!out)
		return;
	T_CHECK(strstr(out, "device 3-6: 1c7a:0587  ETU905A88-E  by EGIS"));
	T_CHECK(strstr(out, "libfprint: driver egismoc, Match-on-Chip"));
	T_CHECK(strstr(out, "fpal:      supported, driver egis_moc"));
	T_CHECK(strstr(out, "480 Mb/s"));
	T_CHECK(strstr(out, "endpoint 0x02 OUT Bulk      max packet 512"));
	T_CHECK(strstr(out, "endpoint 0x81 IN  Bulk      max packet 512"));
	T_CHECK(strstr(out, "endpoint 0x83 IN  Interrupt max packet 64"));
	/* endpoints come out in address order whatever readdir gave us */
	T_CHECK(strstr(out, "0x02") < strstr(out, "0x81"));
	T_CHECK(strstr(out, "0x81") < strstr(out, "0x83"));
	free(out);
}

static void test_the_serial_number_is_never_printed(void) {
	char *out = render(true, false, FPAL_REPORT_DEVS);

	T_CHECK(out != NULL);
	if (out)
		T_CHECK(!strstr(out, "SECRET"));
	free(out);
}

static void test_who_is_a_candidate(void) {
	char *out = render(false, false, FPAL_REPORT_DEVS);

	T_CHECK(out != NULL);
	if (!out)
		return;
	T_CHECK(!strstr(out, "xHCI"));	      /* a hub is left out */
	T_CHECK(strstr(out, "dead:beef"));    /* vendor-specific interface */
	T_CHECK(strstr(out, "not known to it")); /* ... libfprint has never seen */
	T_CHECK(strstr(out, "Acme Fingerprint")); /* by name */
	T_CHECK(!strstr(out, "0c45:7000"));   /* an ordinary keyboard */
	free(out);

	out = render(true, false, FPAL_REPORT_DEVS);
	T_CHECK(out && strstr(out, "xHCI") && strstr(out, "0c45:7000"));
	free(out);
}

static void test_device_strings_cannot_drive_the_terminal(void) {
	char *out = render(true, false, FPAL_REPORT_DEVS);

	T_CHECK(out != NULL);
	if (!out)
		return;
	T_CHECK(!strchr(out, '\x1b'));
	T_CHECK(strstr(out, "Key?[2Jboard"));
	free(out);
}

static void test_markdown_is_fenced(void) {
	char *out = render(false, true, FPAL_REPORT_DEVS);

	T_CHECK(out != NULL);
	if (!out)
		return;
	T_CHECK(!strncmp(out, "```\n", 4));
	T_CHECK(strlen(out) > 8 && !strcmp(out + strlen(out) - 4, "```\n"));
	free(out);
}

static void test_limits_and_errors(void) {
	struct fpal_report_dev devs[FPAL_REPORT_DEVS];
	size_t n = 99;

	T_EQ_INT(fpal_report_scan("/nonexistent/sysfs", false, devs, 8, &n),
		 FPAL_E_IO);
	T_EQ_INT(fpal_report_scan(NULL, false, devs, 8, &n), FPAL_E_INVAL);
	T_EQ_INT(fpal_report_scan(root, true, devs, 2, &n), FPAL_OK);
	T_EQ_INT(n, 2); /* never writes past what the caller offered */
}

static void test_empty_sysfs(void) {
	char empty[80];
	struct fpal_report_dev devs[FPAL_REPORT_DEVS];
	size_t n = 9, size = 0;
	char *buf = NULL;
	FILE *f;

	snprintf(empty, sizeof(empty), "%s-empty", root);
	T_EQ_INT(mkdir(empty, 0700), 0);
	T_EQ_INT(fpal_report_scan(empty, false, devs, 8, &n), FPAL_OK);
	T_EQ_INT(n, 0);
	f = open_memstream(&buf, &size);
	fpal_report_print(f, devs, n, false);
	fclose(f);
	T_CHECK(strstr(buf, "no USB fingerprint sensor candidates found"));
	free(buf);
	rmdir(empty);
}

int main(void) {
	char cmd[96];
	int rc;

	snprintf(root, sizeof(root), "/tmp/fpal-report-XXXXXX");
	if (!mkdtemp(root)) {
		perror("mkdtemp");
		return 1;
	}
	build_tree();

	T_RUN(test_a_known_sensor_is_described);
	T_RUN(test_the_serial_number_is_never_printed);
	T_RUN(test_who_is_a_candidate);
	T_RUN(test_device_strings_cannot_drive_the_terminal);
	T_RUN(test_markdown_is_fenced);
	T_RUN(test_limits_and_errors);
	T_RUN(test_empty_sysfs);
	rc = T_DONE();

	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
	if (system(cmd) != 0)
		perror("cleanup");
	return rc;
}
