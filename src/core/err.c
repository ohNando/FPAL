// SPDX-License-Identifier: LGPL-2.1-or-later
#include "fpx/fpx.h"

const char *fpx_strerror(int err)
{
	switch (err) {
	case FPX_OK:
		return "success";
	case FPX_E_INVAL:
		return "invalid argument";
	case FPX_E_NOMEM:
		return "out of memory";
	case FPX_E_NODEV:
		return "no supported fingerprint sensor found";
	case FPX_E_PERM:
		return "permission denied (run as root)";
	case FPX_E_BUSY:
		return "sensor is in use by another process (is fprintd running?)";
	case FPX_E_IO:
		return "USB transfer failed";
	case FPX_E_TIMEOUT:
		return "timed out";
	case FPX_E_CANCELLED:
		return "cancelled";
	case FPX_E_PROTO:
		return "unexpected response from sensor";
	case FPX_E_NOMATCH:
		return "finger not recognised";
	case FPX_E_NODATA:
		return "no fingerprints stored on the sensor";
	case FPX_E_NOTSUP:
		return "operation not supported";
	case FPX_E_OVERFLOW:
		return "buffer too small";
	default:
		return "unknown error";
	}
}
