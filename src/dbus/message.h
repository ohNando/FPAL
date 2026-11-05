/* SPDX-License-Identifier: Apache-2.0 */
/*
 * D-Bus message framing: a fixed 16 byte header, an array of header fields
 * (path, interface, member, ...) padded to 8 bytes, then the body.
 *
 * Messages are little-endian only; a big-endian message is rejected.
 * Parsed strings point into the buffer handed to fpal_dmsg_parse().
 */
#ifndef FPAL_DBUS_MESSAGE_H
#define FPAL_DBUS_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

#include "fpal/types.h"

enum fpal_dmsg_type {
	FPAL_DMSG_CALL = 1,
	FPAL_DMSG_RETURN = 2,
	FPAL_DMSG_ERROR = 3,
	FPAL_DMSG_SIGNAL = 4,
};

#define FPAL_DMSG_NO_REPLY 0x01u /* flag: caller wants no answer */

/* Size of the fixed part; enough to learn the total length of a message. */
#define FPAL_DMSG_FIXED 16u

struct fpal_dmsg {
	uint8_t type;
	uint8_t flags;
	uint32_t serial;
	uint32_t reply_serial; /* 0 = absent (serials start at 1) */
	const char *path;      /* NULL = absent, as are the other strings */
	const char *iface;
	const char *member;
	const char *errname;
	const char *dest;
	const char *sender;
	const char *sig; /* body signature, NULL or "" for an empty body */
	const uint8_t *body;
	uint32_t body_len;
};

/*
 * Size of the message whose first FPAL_DMSG_FIXED bytes are @hdr, however big
 * it is. FPAL_E_PROTO for a header that is not one: the byte stream cannot be
 * trusted any more.
 */
int fpal_dmsg_frame_len(const uint8_t *hdr, uint64_t *total);

/*
 * The same, for a message we are willing to hold: FPAL_E_OVERFLOW if it is
 * larger than FPAL_DBUS_MSG_MAX.
 */
int fpal_dmsg_total_len(const uint8_t *hdr, size_t *total);

/* Parse and validate one complete message of exactly @len bytes. */
int fpal_dmsg_parse(const uint8_t *buf, size_t len, struct fpal_dmsg *m);

/*
 * Serialise @m (type, flags, serial, the strings that are non-NULL,
 * reply_serial if non-zero, and the body) into @out.
 */
int fpal_dmsg_build(uint8_t *out, size_t cap, size_t *outlen,
		    const struct fpal_dmsg *m);

#endif /* FPAL_DBUS_MESSAGE_H */
