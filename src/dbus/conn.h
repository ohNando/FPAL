/* SPDX-License-Identifier: Apache-2.0 */
/*
 * A blocking D-Bus client connection over a Unix socket: SASL EXTERNAL
 * handshake, Hello, message send/receive, and helpers for answering calls.
 *
 * Everything fits in one fixed buffer (no heap). The connection never
 * negotiates file descriptor passing.
 */
#ifndef FPAL_DBUS_CONN_H
#define FPAL_DBUS_CONN_H

#include <stddef.h>
#include <stdint.h>

#include "dbus/message.h"
#include "fpal/config.h"

#define FPAL_DBUS_NAME_MAX 64u

struct fpal_dbus {
	int fd;
	uint32_t next_serial;
	char unique[FPAL_DBUS_NAME_MAX]; /* our name on the bus, ":1.42" */
	uint8_t rx[FPAL_DBUS_MSG_MAX];
	size_t rx_len;	  /* bytes buffered */
	size_t rx_handed; /* bytes of the last returned message, to drop */
	uint64_t skip;	  /* bytes of an oversize message still to discard */
};

/*
 * Connect to the bus socket at @path, authenticate as the calling user and
 * send Hello. @path NULL means FPAL_DBUS_SYSTEM_SOCKET.
 */
int fpal_dbus_open(struct fpal_dbus *c, const char *path);

/* Same, on an already connected socket (used by tests). Takes ownership. */
int fpal_dbus_attach(struct fpal_dbus *c, int fd);

void fpal_dbus_close(struct fpal_dbus *c);

uint32_t fpal_dbus_serial(struct fpal_dbus *c);

/* Send @m; if m->serial is 0 a fresh one is assigned in a local copy. */
int fpal_dbus_send(struct fpal_dbus *c, const struct fpal_dmsg *m);

/*
 * Wait up to @timeout_ms for the next complete message. FPAL_E_TIMEOUT if
 * none arrived (partial data stays buffered). The returned message points
 * into the connection and is valid until the next fpal_dbus_recv().
 *
 * A message that is too big to hold, or whose contents are malformed, is
 * thrown away and reported once as FPAL_E_DROPPED; the connection is still
 * good and the caller just asks again. Any other error (the bus hung up, or
 * the bytes stopped being messages at all) ends the connection.
 */
int fpal_dbus_recv(struct fpal_dbus *c, uint32_t timeout_ms,
		   struct fpal_dmsg *m);

/*
 * Send a method call and wait for its answer (a RETURN or an ERROR; check
 * reply->type). Unrelated messages that arrive meanwhile are dropped.
 */
int fpal_dbus_call(struct fpal_dbus *c, const struct fpal_dmsg *call,
		   uint32_t timeout_ms, struct fpal_dmsg *reply);

/* Answer @call with a RETURN carrying @body (signature @sig). */
int fpal_dbus_reply(struct fpal_dbus *c, const struct fpal_dmsg *call,
		    const char *sig, const uint8_t *body, size_t body_len);

/* Answer @call with an ERROR whose message text is @text (may be NULL). */
int fpal_dbus_reply_error(struct fpal_dbus *c, const struct fpal_dmsg *call,
			  const char *errname, const char *text);

/* ---- bus daemon helpers ------------------------------------------------ */

#define FPAL_DBUS_NAME_FLAG_DO_NOT_QUEUE 0x4u

#define FPAL_DBUS_REPLY_PRIMARY_OWNER 1u
#define FPAL_DBUS_REPLY_IN_QUEUE 2u
#define FPAL_DBUS_REPLY_EXISTS 3u
#define FPAL_DBUS_REPLY_ALREADY_OWNER 4u

/* org.freedesktop.DBus.RequestName; @result gets one of the REPLY codes. */
int fpal_dbus_request_name(struct fpal_dbus *c, const char *name,
			   uint32_t flags, uint32_t *result);

#endif /* FPAL_DBUS_CONN_H */
