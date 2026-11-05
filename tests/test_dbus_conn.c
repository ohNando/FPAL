// SPDX-License-Identifier: Apache-2.0
/*
 * Connection tests against a fake bus: one end of a socketpair is the client
 * under test, the other end is driven by the test. Replies are written
 * before the client asks, which works because the socket buffers them.
 */
#include "fake_bus.h"
#include "core/bytes.h"

static void test_handshake_and_hello(void) {
	struct fake f;
	uint8_t sent[512];
	char want[64];
	size_t n, off;
	struct fpal_dmsg m;

	T_EQ_INT(fake_open(&f), FPAL_OK);
	T_CHECK(!strcmp(f.c.unique, ":1.77"));

	/* "\0AUTH EXTERNAL <hex of decimal uid>\r\nBEGIN\r\n" */
	snprintf(want, sizeof(want), "AUTH EXTERNAL ");
	for (char dec[16], *d = (snprintf(dec, sizeof(dec), "%u",
					  (unsigned)geteuid()), dec);
	     *d; d++)
		snprintf(want + strlen(want), sizeof(want) - strlen(want),
			 "%02x", *d);
	strcat(want, "\r\nBEGIN\r\n");

	n = srv_read(f.peer, sent, sizeof(sent));
	T_CHECK(n > 1 + strlen(want));
	T_EQ_INT(sent[0], 0);
	T_EQ_MEM(sent + 1, want, strlen(want));

	off = 1 + strlen(want);
	T_EQ_INT(fpal_dmsg_parse(sent + off, n - off, &m), FPAL_OK);
	T_EQ_INT(m.type, FPAL_DMSG_CALL);
	T_EQ_INT(m.serial, 1);
	T_CHECK(!strcmp(m.member, "Hello"));
	T_CHECK(!strcmp(m.dest, "org.freedesktop.DBus"));

	fpal_dbus_close(&f.c);
	close(f.peer);
}

static void test_auth_rejected(void) {
	struct fake f;
	int sv[2];

	T_EQ_INT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
	srv_write(sv[1], "REJECTED EXTERNAL\r\n", 19);
	T_EQ_INT(fpal_dbus_attach(&f.c, sv[0]), FPAL_E_PERM);
	close(sv[1]);
}

static void test_request_name_skips_signals(void) {
	struct fake f;
	uint8_t body[16], sent[1024];
	struct fpal_dw w;
	struct fpal_dmsg m, sig = { .type = FPAL_DMSG_SIGNAL, .serial = 2,
				    .path = "/org/freedesktop/DBus",
				    .iface = "org.freedesktop.DBus",
				    .member = "NameAcquired" };
	struct fpal_dmsg ret = { .type = FPAL_DMSG_RETURN, .serial = 3,
				 .reply_serial = 2, .sig = "u" };
	uint32_t res = 0;
	size_t n, total;
	struct fpal_dr r;

	T_EQ_INT(fake_open(&f), FPAL_OK);
	(void)srv_read(f.peer, sent, sizeof(sent));

	srv_msg(f.peer, &sig); /* arrives before the answer, must be skipped */
	fpal_dw_init(&w, body, sizeof(body));
	fpal_dw_u32(&w, FPAL_DBUS_REPLY_PRIMARY_OWNER);
	ret.body = body;
	ret.body_len = (uint32_t)w.len;
	srv_msg(f.peer, &ret);

	T_EQ_INT(fpal_dbus_request_name(&f.c, "net.reactivated.Fprint",
					FPAL_DBUS_NAME_FLAG_DO_NOT_QUEUE, &res),
		 FPAL_OK);
	T_EQ_INT(res, FPAL_DBUS_REPLY_PRIMARY_OWNER);

	n = srv_read(f.peer, sent, sizeof(sent));
	T_EQ_INT(fpal_dmsg_total_len(sent, &total), FPAL_OK);
	T_EQ_INT(total, n);
	T_EQ_INT(fpal_dmsg_parse(sent, n, &m), FPAL_OK);
	T_EQ_INT(m.serial, 2);
	T_CHECK(!strcmp(m.member, "RequestName"));
	T_CHECK(!strcmp(m.sig, "su"));
	fpal_dr_init(&r, m.body, m.body_len);
	T_CHECK(!strcmp(fpal_dr_str(&r), "net.reactivated.Fprint"));
	T_EQ_INT(fpal_dr_u32(&r), FPAL_DBUS_NAME_FLAG_DO_NOT_QUEUE);

	fpal_dbus_close(&f.c);
	close(f.peer);
}

static void test_request_name_access_denied(void) {
	struct fake f;
	struct fpal_dmsg err = { .type = FPAL_DMSG_ERROR, .serial = 2,
				 .reply_serial = 2,
				 .errname = "org.freedesktop.DBus.Error.AccessDenied" };
	uint32_t res = 0;

	T_EQ_INT(fake_open(&f), FPAL_OK);
	srv_msg(f.peer, &err);
	T_EQ_INT(fpal_dbus_request_name(&f.c, "x.y", 0, &res), FPAL_E_PERM);
	fpal_dbus_close(&f.c);
	close(f.peer);
}

static void test_recv_partial_then_complete(void) {
	struct fake f;
	uint8_t b[256];
	size_t n;
	struct fpal_dmsg in = { .type = FPAL_DMSG_SIGNAL, .serial = 9,
				.path = "/p", .iface = "a.b", .member = "S" };
	struct fpal_dmsg m;

	T_EQ_INT(fake_open(&f), FPAL_OK);
	T_EQ_INT(fpal_dmsg_build(b, sizeof(b), &n, &in), FPAL_OK);

	srv_write(f.peer, b, n / 2);
	T_EQ_INT(fpal_dbus_recv(&f.c, 0, &m), FPAL_E_TIMEOUT);
	srv_write(f.peer, b + n / 2, n - n / 2);
	T_EQ_INT(fpal_dbus_recv(&f.c, 100, &m), FPAL_OK);
	T_CHECK(!strcmp(m.member, "S"));

	fpal_dbus_close(&f.c);
	close(f.peer);
}

static void test_recv_two_messages_in_one_read(void) {
	struct fake f;
	uint8_t b[512];
	size_t n1, n2;
	struct fpal_dmsg a = { .type = FPAL_DMSG_SIGNAL, .serial = 4,
			       .path = "/p", .iface = "a.b", .member = "One" };
	struct fpal_dmsg c = { .type = FPAL_DMSG_SIGNAL, .serial = 5,
			       .path = "/p", .iface = "a.b", .member = "Two" };
	struct fpal_dmsg m;

	T_EQ_INT(fake_open(&f), FPAL_OK);
	T_EQ_INT(fpal_dmsg_build(b, sizeof(b), &n1, &a), FPAL_OK);
	T_EQ_INT(fpal_dmsg_build(b + n1, sizeof(b) - n1, &n2, &c), FPAL_OK);
	srv_write(f.peer, b, n1 + n2);

	T_EQ_INT(fpal_dbus_recv(&f.c, 100, &m), FPAL_OK);
	T_CHECK(!strcmp(m.member, "One"));
	T_EQ_INT(fpal_dbus_recv(&f.c, 100, &m), FPAL_OK);
	T_CHECK(!strcmp(m.member, "Two"));
	T_EQ_INT(fpal_dbus_recv(&f.c, 0, &m), FPAL_E_TIMEOUT);

	fpal_dbus_close(&f.c);
	close(f.peer);
}

static void test_reply_and_error_helpers(void) {
	struct fake f;
	uint8_t sent[512];
	struct fpal_dmsg call = { .type = FPAL_DMSG_CALL, .serial = 31,
				  .sender = ":1.9", .path = "/p",
				  .member = "M" };
	struct fpal_dmsg m;
	struct fpal_dr r;
	size_t n;

	T_EQ_INT(fake_open(&f), FPAL_OK);
	(void)srv_read(f.peer, sent, sizeof(sent));

	T_EQ_INT(fpal_dbus_reply_error(&f.c, &call,
				       "net.reactivated.Fprint.Error.PermissionDenied",
				       "nope"),
		 FPAL_OK);
	n = srv_read(f.peer, sent, sizeof(sent));
	T_EQ_INT(fpal_dmsg_parse(sent, n, &m), FPAL_OK);
	T_EQ_INT(m.type, FPAL_DMSG_ERROR);
	T_EQ_INT(m.reply_serial, 31);
	T_CHECK(!strcmp(m.dest, ":1.9"));
	fpal_dr_init(&r, m.body, m.body_len);
	T_CHECK(!strcmp(fpal_dr_str(&r), "nope"));

	T_EQ_INT(fpal_dbus_reply(&f.c, &call, NULL, NULL, 0), FPAL_OK);
	n = srv_read(f.peer, sent, sizeof(sent));
	T_EQ_INT(fpal_dmsg_parse(sent, n, &m), FPAL_OK);
	T_EQ_INT(m.type, FPAL_DMSG_RETURN);
	T_EQ_INT(m.reply_serial, 31);

	/* a caller that asked for no reply gets none */
	call.flags = FPAL_DMSG_NO_REPLY;
	T_EQ_INT(fpal_dbus_reply(&f.c, &call, NULL, NULL, 0), FPAL_OK);
	T_EQ_INT(srv_read(f.peer, sent, sizeof(sent)), 0);

	fpal_dbus_close(&f.c);
	close(f.peer);
}

static void test_bus_closed(void) {
	struct fake f;
	struct fpal_dmsg m;

	T_EQ_INT(fake_open(&f), FPAL_OK);
	close(f.peer);
	T_EQ_INT(fpal_dbus_recv(&f.c, 100, &m), FPAL_E_IO);
	fpal_dbus_close(&f.c);
}

static void test_garbage_is_refused(void) {
	struct fake f;
	uint8_t junk[32];
	struct fpal_dmsg m;

	T_EQ_INT(fake_open(&f), FPAL_OK);
	memset(junk, 0x41, sizeof(junk));
	srv_write(f.peer, junk, sizeof(junk));
	T_EQ_INT(fpal_dbus_recv(&f.c, 100, &m), FPAL_E_PROTO);
	fpal_dbus_close(&f.c);
	close(f.peer);
}

/* A message header that claims a body of @body bytes (valid framing). */
static size_t big_header(uint8_t *out, uint32_t body) {
	struct fpal_dmsg m = { .type = FPAL_DMSG_SIGNAL, .serial = 77, .path = "/p",
			       .iface = "a.b", .member = "Big", .sig = "ay" };
	uint8_t tmp[256];
	size_t n;

	m.body = tmp;
	m.body_len = 0;
	T_EQ_INT(fpal_dmsg_build(out, 256, &n, &m), FPAL_OK);
	/* patch the body length field: the bytes after the header are not sent here */
	fpal_put_le32(out + 4, body);
	(void)tmp;
	return n;
}

static void test_oversize_message_is_skipped_not_fatal(void) {
	struct fake f;
	uint8_t hdr[256], filler[4096], b[256];
	struct fpal_dmsg ok = { .type = FPAL_DMSG_SIGNAL, .serial = 8, .path = "/p",
				.iface = "a.b", .member = "After" };
	struct fpal_dmsg m;
	size_t hn, n, sent = 0;
	const uint32_t body = 100000;

	T_EQ_INT(fake_open(&f), FPAL_OK);
	hn = big_header(hdr, body);
	memset(filler, 0x41, sizeof(filler));

	/* the header alone is enough to know it is too big */
	srv_write(f.peer, hdr, hn);
	T_EQ_INT(fpal_dbus_recv(&f.c, 100, &m), FPAL_E_DROPPED);
	T_CHECK(f.c.skip > 0);

	/* its body arrives in pieces, none of it is kept */
	while (sent < body) {
		size_t k = body - sent > sizeof(filler) ? sizeof(filler) : body - sent;

		srv_write(f.peer, filler, k);
		sent += k;
		T_EQ_INT(fpal_dbus_recv(&f.c, 0, &m) == FPAL_E_TIMEOUT ||
				 f.c.skip == 0,
			 1);
	}
	/* and the next message is read normally */
	T_EQ_INT(fpal_dmsg_build(b, sizeof(b), &n, &ok), FPAL_OK);
	srv_write(f.peer, b, n);
	T_EQ_INT(fpal_dbus_recv(&f.c, 100, &m), FPAL_OK);
	T_CHECK(!strcmp(m.member, "After"));
	T_EQ_INT(f.c.skip, 0);
	T_CHECK(f.c.rx_len < FPAL_DBUS_MSG_MAX); /* nothing piled up */

	fpal_dbus_close(&f.c);
	close(f.peer);
}

static void test_malformed_message_is_dropped_not_fatal(void) {
	struct fake f;
	uint8_t b[256];
	/* well framed, but a call needs a member */
	struct fpal_dmsg bad = { .type = FPAL_DMSG_CALL, .serial = 5, .path = "/p" };
	struct fpal_dmsg ok = { .type = FPAL_DMSG_SIGNAL, .serial = 6, .path = "/p",
				.iface = "a.b", .member = "Fine" };
	struct fpal_dmsg m;
	size_t n1, n2;

	T_EQ_INT(fake_open(&f), FPAL_OK);
	T_EQ_INT(fpal_dmsg_build(b, sizeof(b), &n1, &bad), FPAL_OK);
	T_EQ_INT(fpal_dmsg_build(b + n1, sizeof(b) - n1, &n2, &ok), FPAL_OK);
	srv_write(f.peer, b, n1 + n2);

	T_EQ_INT(fpal_dbus_recv(&f.c, 100, &m), FPAL_E_DROPPED);
	T_EQ_INT(fpal_dbus_recv(&f.c, 100, &m), FPAL_OK);
	T_CHECK(!strcmp(m.member, "Fine"));

	fpal_dbus_close(&f.c);
	close(f.peer);
}

static void test_call_ignores_dropped_messages(void) {
	struct fake f;
	uint8_t hdr[256], filler[2048], b[256];
	struct fpal_dmsg call = { .type = FPAL_DMSG_CALL, .path = "/p", .member = "M",
				  .dest = "x.y" };
	struct fpal_dmsg reply = { .type = FPAL_DMSG_RETURN, .serial = 30,
				   .reply_serial = 2 };
	struct fpal_dmsg got;
	size_t hn, n;

	T_EQ_INT(fake_open(&f), FPAL_OK);
	/* something huge arrives first, then the answer to our call (serial 2) */
	hn = big_header(hdr, 30000);
	srv_write(f.peer, hdr, hn);
	memset(filler, 0, sizeof(filler));
	for (size_t sent = 0; sent < 30000; sent += sizeof(filler))
		srv_write(f.peer, filler, 30000 - sent < sizeof(filler) ? 30000 - sent
									: sizeof(filler));
	T_EQ_INT(fpal_dmsg_build(b, sizeof(b), &n, &reply), FPAL_OK);
	srv_write(f.peer, b, n);

	T_EQ_INT(fpal_dbus_call(&f.c, &call, 500, &got), FPAL_OK);
	T_EQ_INT(got.type, FPAL_DMSG_RETURN);

	fpal_dbus_close(&f.c);
	close(f.peer);
}

int main(void) {
	T_RUN(test_handshake_and_hello);
	T_RUN(test_auth_rejected);
	T_RUN(test_request_name_skips_signals);
	T_RUN(test_request_name_access_denied);
	T_RUN(test_recv_partial_then_complete);
	T_RUN(test_recv_two_messages_in_one_read);
	T_RUN(test_reply_and_error_helpers);
	T_RUN(test_bus_closed);
	T_RUN(test_garbage_is_refused);
	T_RUN(test_oversize_message_is_skipped_not_fatal);
	T_RUN(test_malformed_message_is_dropped_not_fatal);
	T_RUN(test_call_ignores_dropped_messages);
	return T_DONE();
}
