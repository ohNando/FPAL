/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Transport abstraction. Drivers talk to hardware only through these ops, so
 * the real usbdevfs backend and the test mock are interchangeable.
 *
 * All functions return FPX_OK or a negative fpx_err.
 */
#ifndef FPX_TRANSPORT_TRANSPORT_H
#define FPX_TRANSPORT_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

/* bmRequestType bits (USB 2.0 spec, table 9-2). */
#define FPX_USB_DIR_IN 0x80u
#define FPX_USB_TYPE_STANDARD 0x00u
#define FPX_USB_TYPE_VENDOR 0x40u
#define FPX_USB_RECIP_DEVICE 0x00u

struct fpx_transport_ops {
	/* Reset the device; any interface claim is lost. */
	int (*reset)(void *ctx);
	int (*claim)(void *ctx, uint8_t iface);
	void (*release)(void *ctx, uint8_t iface);

	/* Device-to-host control transfer; @len bytes must be returned. */
	int (*control_in)(void *ctx, uint8_t req_type, uint8_t req,
			  uint16_t value, uint16_t index, uint8_t *buf,
			  uint16_t len, uint32_t timeout_ms);

	int (*bulk_out)(void *ctx, uint8_t ep, const uint8_t *buf, size_t len,
			uint32_t timeout_ms);
	int (*bulk_in)(void *ctx, uint8_t ep, uint8_t *buf, size_t cap,
		       size_t *got, uint32_t timeout_ms);

	/*
	 * Wait for one interrupt IN packet. Unlike the bulk calls this wait
	 * can be aborted with cancel(); returns FPX_E_CANCELLED then.
	 */
	int (*wait_interrupt)(void *ctx, uint8_t ep, uint8_t *buf, size_t cap,
			      size_t *got, uint32_t timeout_ms);

	/* Abort a pending wait_interrupt(). Must be async-signal-safe. */
	void (*cancel)(void *ctx);

	void (*close)(void *ctx);
};

struct fpx_transport {
	const struct fpx_transport_ops *ops;
	void *ctx;
};

/* Open /dev/bus/usb/<bus>/<dev> through usbdevfs. */
int fpx_usbdevfs_open(uint8_t busnum, uint8_t devnum,
		      struct fpx_transport *out);

#endif /* FPX_TRANSPORT_TRANSPORT_H */
