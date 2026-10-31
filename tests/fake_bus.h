/* SPDX-License-Identifier: Apache-2.0 */
/*
 * A fake D-Bus daemon for tests: one end of a socketpair is the client under
 * test, the other end is driven by the test. Replies are written before the
 * client asks, which works because the socket buffers them.
 */
#ifndef FPAL_TESTS_FAKE_BUS_H
#define FPAL_TESTS_FAKE_BUS_H

#include <sys/socket.h>
#include <unistd.h>

#include "dbus/conn.h"
#include "dbus/marshal.h"
#include "t.h"

#define AUTH_OK "OK 0123456789abcdef0123456789abcdef\r\n"

struct fake {
	int peer;
	struct fpal_dbus c;
};

static inline void srv_write(int fd, const void *p, size_t n) {
	T_EQ_INT(write(fd, p, n), (ssize_t)n);
}

static inline void srv_msg(int fd, const struct fpal_dmsg *m) {
	uint8_t b[512];
	size_t n;

	T_EQ_INT(fpal_dmsg_build(b, sizeof(b), &n, m), FPAL_OK);
	srv_write(fd, b, n);
}

static inline void srv_hello_reply(int fd, uint32_t srv_serial, const char *name) {
	uint8_t body[64];
	struct fpal_dw w;
	struct fpal_dmsg m = { .type = FPAL_DMSG_RETURN, .serial = srv_serial,
			       .reply_serial = 1, .sig = "s",
			       .sender = "org.freedesktop.DBus" };

	fpal_dw_init(&w, body, sizeof(body));
	fpal_dw_str(&w, name);
	m.body = body;
	m.body_len = (uint32_t)w.len;
	srv_msg(fd, &m);
}

/* Everything the client has written so far. */
static inline size_t srv_read(int fd, uint8_t *buf, size_t cap) {
	ssize_t n = recv(fd, buf, cap, MSG_DONTWAIT);

	return n < 0 ? 0 : (size_t)n;
}

static inline int fake_open(struct fake *f) {
	int sv[2];

	T_EQ_INT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
	f->peer = sv[1];
	srv_write(f->peer, AUTH_OK, sizeof(AUTH_OK) - 1);
	srv_hello_reply(f->peer, 1, ":1.77");
	return fpal_dbus_attach(&f->c, sv[0]);
}

#endif /* FPAL_TESTS_FAKE_BUS_H */
