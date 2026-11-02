// SPDX-License-Identifier: Apache-2.0
#include "fpal/fpal.h"

const char *fpal_strerror(int err) {
	switch (err) {
	case FPAL_OK:
		return "success";
	case FPAL_E_INVAL:
		return "invalid argument";
	case FPAL_E_NOMEM:
		return "out of memory";
	case FPAL_E_NODEV:
		return "no supported fingerprint sensor found";
	case FPAL_E_PERM:
		return "permission denied (run as root)";
	case FPAL_E_BUSY:
		return "sensor is in use by another process (is fprintd running?)";
	case FPAL_E_IO:
		return "USB transfer failed";
	case FPAL_E_TIMEOUT:
		return "timed out";
	case FPAL_E_CANCELLED:
		return "cancelled";
	case FPAL_E_PROTO:
		return "unexpected response from sensor";
	case FPAL_E_NOMATCH:
		return "finger not recognised";
	case FPAL_E_NODATA:
		return "no fingerprints stored on the sensor";
	case FPAL_E_NOTSUP:
		return "operation not supported";
	case FPAL_E_OVERFLOW:
		return "buffer too small";
	case FPAL_E_FULL:
		return "the sensor cannot store more fingerprints";
	case FPAL_E_EXISTS:
		return "this finger is already enrolled";
	case FPAL_E_DROPPED:
		return "a message was dropped";
	default:
		return "unknown error";
	}
}
