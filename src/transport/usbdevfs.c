// SPDX-License-Identifier: Apache-2.0
/*
 * usbdevfs transport: talks to the kernel's USB core directly through
 * ioctl()s on /dev/bus/usb/BBB/DDD, without libusb.
 *
 * Command traffic uses the synchronous USBDEVFS_BULK ioctl. The finger wait
 * on the interrupt endpoint uses an asynchronous URB so it can be cancelled:
 * a synchronous transfer would block uninterruptibly in the kernel.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/usbdevice_fs.h>

#include "core/log.h"
#include "core/trace.h"
#include "fpx/fpx.h"
#include "transport.h"

struct usbdevfs_ctx {
	int fd;
	int cancel_pipe[2]; /* [0] polled by wait_interrupt, [1] written by cancel */
};

static int errno_to_fpx(int e) {
	switch (e) {
	case EACCES:
	case EPERM:
		return FPX_E_PERM;
	case EBUSY:
		return FPX_E_BUSY;
	case ETIMEDOUT:
		return FPX_E_TIMEOUT;
	case ENODEV:
	case ENOENT:
		return FPX_E_NODEV;
	case ENOMEM:
		return FPX_E_NOMEM;
	default:
		return FPX_E_IO;
	}
}

static int ud_reset(void *ctx) {
	struct usbdevfs_ctx *c = ctx;

	if (ioctl(c->fd, USBDEVFS_RESET, NULL) < 0)
		return errno_to_fpx(errno);
	return FPX_OK;
}

static int ud_claim(void *ctx, uint8_t iface) {
	struct usbdevfs_ctx *c = ctx;
	struct usbdevfs_getdriver gd = { .interface = iface };
	unsigned int ifno = iface;

	/*
	 * If a driver is already bound, either the kernel owns the interface
	 * or another userspace program (driver name "usbfs", e.g. fprintd)
	 * has claimed it. Never steal it.
	 */
	if (ioctl(c->fd, USBDEVFS_GETDRIVER, &gd) == 0) {
		fpx_dbg("interface %u already bound to driver '%s'", iface,
			gd.driver);
		return FPX_E_BUSY;
	}

	if (ioctl(c->fd, USBDEVFS_CLAIMINTERFACE, &ifno) < 0)
		return errno_to_fpx(errno);
	return FPX_OK;
}

static void ud_release(void *ctx, uint8_t iface) {
	struct usbdevfs_ctx *c = ctx;
	unsigned int ifno = iface;

	ioctl(c->fd, USBDEVFS_RELEASEINTERFACE, &ifno);
}

static int ud_control_in(void *ctx, uint8_t req_type, uint8_t req,
			 uint16_t value, uint16_t index, uint8_t *buf,
			 uint16_t len, uint32_t timeout_ms) {
	struct usbdevfs_ctx *c = ctx;
	struct usbdevfs_ctrltransfer ct = {
		.bRequestType = (uint8_t)(req_type | FPX_USB_DIR_IN),
		.bRequest = req,
		.wValue = value,
		.wIndex = index,
		.wLength = len,
		.timeout = timeout_ms,
		.data = buf,
	};
	int rc = ioctl(c->fd, USBDEVFS_CONTROL, &ct);

	if (rc < 0)
		return errno_to_fpx(errno);

	fpx_trace("CTRL", req, buf, (size_t)rc);
	if (rc != len) {
		fpx_dbg("control req %u: short read %d/%u", req, rc, len);
		return FPX_E_IO;
	}
	return FPX_OK;
}

static int ud_bulk(struct usbdevfs_ctx *c, uint8_t ep, void *buf, size_t len,
		   uint32_t timeout_ms) {
	struct usbdevfs_bulktransfer bt = {
		.ep = ep,
		.len = (unsigned int)len,
		.timeout = timeout_ms,
		.data = buf,
	};
	int rc;

	if (len > UINT_MAX)
		return FPX_E_INVAL;

	rc = ioctl(c->fd, USBDEVFS_BULK, &bt);
	if (rc < 0)
		return errno_to_fpx(errno);
	return rc;
}

static int ud_bulk_out(void *ctx, uint8_t ep, const uint8_t *buf, size_t len,
		       uint32_t timeout_ms) {
	/* USBDEVFS_BULK takes a non-const pointer but does not modify OUT data. */
	int rc = ud_bulk(ctx, ep, (void *)(uintptr_t)buf, len, timeout_ms);

	fpx_trace("OUT", ep, buf, len);
	if (rc < 0)
		return rc;
	if ((size_t)rc != len)
		return FPX_E_IO;
	return FPX_OK;
}

static int ud_bulk_in(void *ctx, uint8_t ep, uint8_t *buf, size_t cap,
		      size_t *got, uint32_t timeout_ms) {
	int rc = ud_bulk(ctx, ep, buf, cap, timeout_ms);

	if (rc < 0)
		return rc;
	*got = (size_t)rc;
	fpx_trace("IN", ep, buf, *got);
	return FPX_OK;
}

static void drain_cancel_pipe(struct usbdevfs_ctx *c) {
	uint8_t tmp[16];

	while (read(c->cancel_pipe[0], tmp, sizeof(tmp)) > 0)
		;
}

/* Discard a submitted URB and reap it so the kernel releases our buffer. */
static void discard_urb(struct usbdevfs_ctx *c, struct usbdevfs_urb *urb) {
	void *reaped = NULL;

	ioctl(c->fd, USBDEVFS_DISCARDURB, urb);
	while (ioctl(c->fd, USBDEVFS_REAPURB, &reaped) < 0 && errno == EINTR)
		;
}

static int ud_wait_interrupt(void *ctx, uint8_t ep, uint8_t *buf, size_t cap,
			     size_t *got, uint32_t timeout_ms) {
	struct usbdevfs_ctx *c = ctx;
	struct usbdevfs_urb urb;
	struct pollfd pfd[2];
	void *reaped = NULL;
	int rc;

	if (cap > INT_MAX)
		return FPX_E_INVAL;

	drain_cancel_pipe(c);

	memset(&urb, 0, sizeof(urb));
	urb.type = USBDEVFS_URB_TYPE_INTERRUPT;
	urb.endpoint = ep;
	urb.buffer = buf;
	urb.buffer_length = (int)cap;

	if (ioctl(c->fd, USBDEVFS_SUBMITURB, &urb) < 0)
		return errno_to_fpx(errno);

	/* usbdevfs reports completed URBs as POLLOUT on the device fd. */
	pfd[0] = (struct pollfd){ .fd = c->fd, .events = POLLOUT };
	pfd[1] = (struct pollfd){ .fd = c->cancel_pipe[0], .events = POLLIN };

	for (;;) {
		rc = poll(pfd, 2, timeout_ms > INT_MAX ? -1 : (int)timeout_ms);
		if (rc < 0 && errno == EINTR) {
			/* A signal handler may have called cancel(); re-check. */
			continue;
		}
		break;
	}

	if (rc < 0) {
		discard_urb(c, &urb);
		return FPX_E_IO;
	}
	if (rc == 0) {
		discard_urb(c, &urb);
		return FPX_E_TIMEOUT;
	}
	if (pfd[1].revents & POLLIN) {
		discard_urb(c, &urb);
		drain_cancel_pipe(c);
		return FPX_E_CANCELLED;
	}

	if (ioctl(c->fd, USBDEVFS_REAPURBNDELAY, &reaped) < 0)
		return errno_to_fpx(errno);
	if (reaped != &urb || urb.status != 0) {
		fpx_dbg("interrupt urb failed, status %d", urb.status);
		return FPX_E_IO;
	}

	*got = (size_t)urb.actual_length;
	fpx_trace("IRQ", ep, buf, *got);
	return FPX_OK;
}

static void ud_cancel(void *ctx) {
	struct usbdevfs_ctx *c = ctx;
	const uint8_t one = 1;

	/* write() is async-signal-safe; the pipe is non-blocking. */
	(void)!write(c->cancel_pipe[1], &one, 1);
}

static void ud_close(void *ctx) {
	struct usbdevfs_ctx *c = ctx;

	close(c->cancel_pipe[0]);
	close(c->cancel_pipe[1]);
	close(c->fd);
	free(c);
}

static const struct fpx_transport_ops usbdevfs_ops = {
	.reset = ud_reset,
	.claim = ud_claim,
	.release = ud_release,
	.control_in = ud_control_in,
	.bulk_out = ud_bulk_out,
	.bulk_in = ud_bulk_in,
	.wait_interrupt = ud_wait_interrupt,
	.cancel = ud_cancel,
	.close = ud_close,
};

int fpx_usbdevfs_open(uint8_t busnum, uint8_t devnum,
		      struct fpx_transport *out) {
	char path[64];
	struct usbdevfs_ctx *c;
	int err;

	snprintf(path, sizeof(path), FPX_DEVFS_USB_FMT, busnum, devnum);

	c = calloc(1, sizeof(*c));
	if (!c)
		return FPX_E_NOMEM;

	c->fd = open(path, O_RDWR | O_CLOEXEC);
	if (c->fd < 0) {
		err = errno_to_fpx(errno);
		fpx_dbg("open %s: %s", path, strerror(errno));
		free(c);
		return err;
	}

	if (pipe2(c->cancel_pipe, O_CLOEXEC | O_NONBLOCK) < 0) {
		err = errno_to_fpx(errno);
		close(c->fd);
		free(c);
		return err;
	}

	out->ops = &usbdevfs_ops;
	out->ctx = c;
	return FPX_OK;
}
