// SPDX-License-Identifier: Apache-2.0
/*
 * D-Bus marshalling and message tests. The vec_* arrays were produced by
 * GLib's GDBus (an independent implementation) so the parser is checked
 * against bytes we did not write ourselves.
 */
#include "dbus/marshal.h"
#include "dbus/message.h"
#include "t.h"

/* ---- reference messages from GDBus ------------------------------------- */

static const uint8_t vec_hello[] = {
	0x6c, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x6e, 0x00, 0x00, 0x00, 0x01, 0x01, 0x6f, 0x00, 0x15, 0x00, 0x00, 0x00,
	0x2f, 0x6f, 0x72, 0x67, 0x2f, 0x66, 0x72, 0x65, 0x65, 0x64, 0x65, 0x73,
	0x6b, 0x74, 0x6f, 0x70, 0x2f, 0x44, 0x42, 0x75, 0x73, 0x00, 0x00, 0x00,
	0x02, 0x01, 0x73, 0x00, 0x14, 0x00, 0x00, 0x00, 0x6f, 0x72, 0x67, 0x2e,
	0x66, 0x72, 0x65, 0x65, 0x64, 0x65, 0x73, 0x6b, 0x74, 0x6f, 0x70, 0x2e,
	0x44, 0x42, 0x75, 0x73, 0x00, 0x00, 0x00, 0x00, 0x06, 0x01, 0x73, 0x00,
	0x14, 0x00, 0x00, 0x00, 0x6f, 0x72, 0x67, 0x2e, 0x66, 0x72, 0x65, 0x65,
	0x64, 0x65, 0x73, 0x6b, 0x74, 0x6f, 0x70, 0x2e, 0x44, 0x42, 0x75, 0x73,
	0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x73, 0x00, 0x05, 0x00, 0x00, 0x00,
	0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x00, 0x00, 0x00,
};

static const uint8_t vec_return_o[] = {
	0x6c, 0x02, 0x01, 0x01, 0x25, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
	0x30, 0x00, 0x00, 0x00, 0x07, 0x01, 0x73, 0x00, 0x04, 0x00, 0x00, 0x00,
	0x3a, 0x31, 0x2e, 0x35, 0x00, 0x00, 0x00, 0x00, 0x06, 0x01, 0x73, 0x00,
	0x04, 0x00, 0x00, 0x00, 0x3a, 0x31, 0x2e, 0x39, 0x00, 0x00, 0x00, 0x00,
	0x08, 0x01, 0x67, 0x00, 0x01, 0x6f, 0x00, 0x00, 0x05, 0x01, 0x75, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x2f, 0x6e, 0x65, 0x74,
	0x2f, 0x72, 0x65, 0x61, 0x63, 0x74, 0x69, 0x76, 0x61, 0x74, 0x65, 0x64,
	0x2f, 0x46, 0x70, 0x72, 0x69, 0x6e, 0x74, 0x2f, 0x44, 0x65, 0x76, 0x69,
	0x63, 0x65, 0x2f, 0x30, 0x00,
};

static const uint8_t vec_return_props[] = {
	0x6c, 0x02, 0x01, 0x01, 0x60, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
	0x28, 0x00, 0x00, 0x00, 0x07, 0x01, 0x73, 0x00, 0x04, 0x00, 0x00, 0x00,
	0x3a, 0x31, 0x2e, 0x35, 0x00, 0x00, 0x00, 0x00, 0x08, 0x01, 0x67, 0x00,
	0x05, 0x61, 0x7b, 0x73, 0x76, 0x7d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x05, 0x01, 0x75, 0x00, 0x03, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x6e, 0x61, 0x6d, 0x65,
	0x00, 0x01, 0x73, 0x00, 0x04, 0x00, 0x00, 0x00, 0x46, 0x50, 0x41, 0x4c,
	0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x73, 0x63, 0x61, 0x6e,
	0x2d, 0x74, 0x79, 0x70, 0x65, 0x00, 0x01, 0x73, 0x00, 0x00, 0x00, 0x00,
	0x05, 0x00, 0x00, 0x00, 0x70, 0x72, 0x65, 0x73, 0x73, 0x00, 0x00, 0x00,
	0x11, 0x00, 0x00, 0x00, 0x6e, 0x75, 0x6d, 0x2d, 0x65, 0x6e, 0x72, 0x6f,
	0x6c, 0x6c, 0x2d, 0x73, 0x74, 0x61, 0x67, 0x65, 0x73, 0x00, 0x01, 0x69,
	0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
};

static const uint8_t vec_error[] = {
	0x6c, 0x03, 0x01, 0x01, 0x0e, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
	0x58, 0x00, 0x00, 0x00, 0x07, 0x01, 0x73, 0x00, 0x04, 0x00, 0x00, 0x00,
	0x3a, 0x31, 0x2e, 0x31, 0x00, 0x00, 0x00, 0x00, 0x04, 0x01, 0x73, 0x00,
	0x29, 0x00, 0x00, 0x00, 0x6e, 0x65, 0x74, 0x2e, 0x72, 0x65, 0x61, 0x63,
	0x74, 0x69, 0x76, 0x61, 0x74, 0x65, 0x64, 0x2e, 0x46, 0x70, 0x72, 0x69,
	0x6e, 0x74, 0x2e, 0x45, 0x72, 0x72, 0x6f, 0x72, 0x2e, 0x4e, 0x6f, 0x53,
	0x75, 0x63, 0x68, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x08, 0x01, 0x67, 0x00, 0x01, 0x73, 0x00, 0x00,
	0x05, 0x01, 0x75, 0x00, 0x04, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
	0x6e, 0x6f, 0x20, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x00,
};


/* ---- marshal ----------------------------------------------------------- */

static void test_alignment(void) {
	uint8_t buf[64];
	struct fpal_dw w;

	fpal_dw_init(&w, buf, sizeof(buf));
	fpal_dw_u8(&w, 0xaa);
	fpal_dw_u32(&w, 0x01020304);
	T_EQ_INT(w.len, 8);
	T_EQ_INT(buf[1], 0); /* padding is zeroed */
	T_EQ_INT(buf[4], 0x04); /* little endian */
	T_EQ_INT(buf[7], 0x01);
	T_EQ_INT(w.err, 0);
}

static void test_string_and_signature(void) {
	static const uint8_t want[] = { 3, 0, 0, 0, 'a', 'b', 'c', 0,
					1, 's', 0 };
	uint8_t buf[32];
	struct fpal_dw w;

	fpal_dw_init(&w, buf, sizeof(buf));
	fpal_dw_str(&w, "abc");
	fpal_dw_sig(&w, "s");
	T_EQ_INT(w.len, sizeof(want));
	T_EQ_MEM(buf, want, sizeof(want));
}

static void test_array_length(void) {
	uint8_t buf[64];
	struct fpal_dw w;
	struct fpal_dw_arr arr;
	struct fpal_dr r;

	fpal_dw_init(&w, buf, sizeof(buf));
	fpal_dw_array_begin(&w, 4, &arr);
	fpal_dw_u32(&w, 1);
	fpal_dw_u32(&w, 2);
	fpal_dw_array_end(&w, &arr);
	T_EQ_INT(w.len, 12);

	fpal_dr_init(&r, buf, w.len);
	T_EQ_INT(fpal_dr_array_begin(&r, 4), 8);
	T_EQ_INT(fpal_dr_u32(&r), 1);
	T_EQ_INT(fpal_dr_u32(&r), 2);
	T_EQ_INT(r.err, 0);
}

static void test_struct_array_pads_to_8(void) {
	uint8_t buf[64];
	struct fpal_dw w;
	struct fpal_dw_arr arr;

	/* Padding before the first element is not counted in the length. */
	fpal_dw_init(&w, buf, sizeof(buf));
	fpal_dw_array_begin(&w, 8, &arr);
	T_EQ_INT(w.len, 8);
	fpal_dw_u8(&w, 7);
	fpal_dw_array_end(&w, &arr);
	T_EQ_INT(buf[0], 1);
}

static void test_writer_overflow_is_sticky(void) {
	uint8_t buf[6];
	struct fpal_dw w;

	fpal_dw_init(&w, buf, sizeof(buf));
	fpal_dw_u32(&w, 1);
	T_EQ_INT(w.err, 0);
	fpal_dw_u32(&w, 2);
	T_EQ_INT(w.err, FPAL_E_OVERFLOW);
	fpal_dw_u8(&w, 3);
	T_EQ_INT(w.len, 4); /* nothing was written after the failure */
}

static void test_reader_rejects_bad_strings(void) {
	static const uint8_t no_nul[] = { 2, 0, 0, 0, 'a', 'b', 'c' };
	static const uint8_t inner_nul[] = { 2, 0, 0, 0, 'a', 0, 0 };
	static const uint8_t truncated[] = { 9, 0, 0, 0, 'a', 0 };
	struct fpal_dr r;

	fpal_dr_init(&r, no_nul, sizeof(no_nul));
	T_CHECK(fpal_dr_str(&r) == NULL);
	T_EQ_INT(r.err, FPAL_E_PROTO);

	fpal_dr_init(&r, inner_nul, sizeof(inner_nul));
	T_CHECK(fpal_dr_str(&r) == NULL);
	T_EQ_INT(r.err, FPAL_E_PROTO);

	fpal_dr_init(&r, truncated, sizeof(truncated));
	T_CHECK(fpal_dr_str(&r) == NULL);
	T_EQ_INT(r.err, FPAL_E_PROTO);
}

static void test_reader_rejects_nonzero_padding(void) {
	static const uint8_t b[] = { 1, 0xff, 0, 0, 5, 0, 0, 0 };
	struct fpal_dr r;

	fpal_dr_init(&r, b, sizeof(b));
	(void)fpal_dr_u8(&r);
	(void)fpal_dr_u32(&r);
	T_EQ_INT(r.err, FPAL_E_PROTO);
}

/* ---- parse GDBus messages ---------------------------------------------- */

static void test_parse_hello(void) {
	struct fpal_dmsg m;

	T_EQ_INT(fpal_dmsg_parse(vec_hello, sizeof(vec_hello), &m), FPAL_OK);
	T_EQ_INT(m.type, FPAL_DMSG_CALL);
	T_EQ_INT(m.serial, 1);
	T_CHECK(!strcmp(m.path, "/org/freedesktop/DBus"));
	T_CHECK(!strcmp(m.iface, "org.freedesktop.DBus"));
	T_CHECK(!strcmp(m.dest, "org.freedesktop.DBus"));
	T_CHECK(!strcmp(m.member, "Hello"));
	T_EQ_INT(m.body_len, 0);
}

static void test_parse_return_with_path(void) {
	struct fpal_dmsg m;
	struct fpal_dr r;

	T_EQ_INT(fpal_dmsg_parse(vec_return_o, sizeof(vec_return_o), &m),
		 FPAL_OK);
	T_EQ_INT(m.type, FPAL_DMSG_RETURN);
	T_EQ_INT(m.serial, 7);
	T_EQ_INT(m.reply_serial, 2);
	T_CHECK(!strcmp(m.sender, ":1.5"));
	T_CHECK(!strcmp(m.dest, ":1.9"));
	T_CHECK(!strcmp(m.sig, "o"));

	fpal_dr_init(&r, m.body, m.body_len);
	T_CHECK(!strcmp(fpal_dr_str(&r), "/net/reactivated/Fprint/Device/0"));
	T_EQ_INT(r.err, 0);
}

static void test_parse_property_dict(void) {
	struct fpal_dmsg m;
	struct fpal_dr r;
	uint32_t n, end;
	unsigned seen = 0;

	T_EQ_INT(fpal_dmsg_parse(vec_return_props, sizeof(vec_return_props),
				 &m),
		 FPAL_OK);
	T_CHECK(!strcmp(m.sig, "a{sv}"));

	fpal_dr_init(&r, m.body, m.body_len);
	n = fpal_dr_array_begin(&r, 8);
	end = (uint32_t)r.off + n;
	while (r.off < end && !r.err) {
		const char *key, *vsig;

		fpal_dr_align(&r, 8);
		key = fpal_dr_str(&r);
		vsig = fpal_dr_sig(&r);
		if (r.err)
			break;
		if (!strcmp(key, "name")) {
			T_CHECK(!strcmp(vsig, "s"));
			T_CHECK(!strcmp(fpal_dr_str(&r), "FPAL"));
			seen |= 1;
		} else if (!strcmp(key, "scan-type")) {
			T_CHECK(!strcmp(fpal_dr_str(&r), "press"));
			seen |= 2;
		} else if (!strcmp(key, "num-enroll-stages")) {
			T_CHECK(!strcmp(vsig, "i"));
			T_EQ_INT((int32_t)fpal_dr_u32(&r), -1);
			seen |= 4;
		}
	}
	T_EQ_INT(r.err, 0);
	T_EQ_INT(seen, 7);
	T_EQ_INT(r.off, m.body_len);
}

static void test_parse_error(void) {
	struct fpal_dmsg m;
	struct fpal_dr r;

	T_EQ_INT(fpal_dmsg_parse(vec_error, sizeof(vec_error), &m), FPAL_OK);
	T_EQ_INT(m.type, FPAL_DMSG_ERROR);
	T_EQ_INT(m.reply_serial, 4);
	T_CHECK(!strcmp(m.errname, "net.reactivated.Fprint.Error.NoSuchDevice"));
	fpal_dr_init(&r, m.body, m.body_len);
	T_CHECK(!strcmp(fpal_dr_str(&r), "no device"));
}

/* ---- framing and rejection --------------------------------------------- */

static void test_total_len(void) {
	size_t total = 0;

	T_EQ_INT(fpal_dmsg_total_len(vec_return_o, &total), FPAL_OK);
	T_EQ_INT(total, sizeof(vec_return_o));
}

static void test_rejects_damaged_messages(void) {
	uint8_t b[sizeof(vec_return_o)];
	struct fpal_dmsg m;
	size_t total;

	/* every truncation must be refused, never read out of bounds */
	for (size_t n = 0; n < sizeof(vec_return_o); n++)
		T_CHECK(fpal_dmsg_parse(vec_return_o, n, &m) != FPAL_OK);

	memcpy(b, vec_return_o, sizeof(b));
	b[0] = 'B'; /* big endian is not supported */
	T_CHECK(fpal_dmsg_total_len(b, &total) != FPAL_OK);

	memcpy(b, vec_return_o, sizeof(b));
	b[3] = 2; /* unknown protocol version */
	T_CHECK(fpal_dmsg_parse(b, sizeof(b), &m) != FPAL_OK);

	memcpy(b, vec_return_o, sizeof(b));
	b[1] = 9; /* unknown message type */
	T_CHECK(fpal_dmsg_parse(b, sizeof(b), &m) != FPAL_OK);

	memcpy(b, vec_return_o, sizeof(b));
	b[8] = b[9] = b[10] = b[11] = 0; /* serial 0 is invalid */
	T_CHECK(fpal_dmsg_parse(b, sizeof(b), &m) != FPAL_OK);

	memcpy(b, vec_return_o, sizeof(b));
	b[7] = 0x7f; /* absurd body length */
	T_EQ_INT(fpal_dmsg_total_len(b, &total), FPAL_E_OVERFLOW);
}

static void test_rejects_call_without_member(void) {
	uint8_t buf[256];
	struct fpal_dmsg m = { .type = FPAL_DMSG_CALL, .serial = 1,
			       .path = "/x" };
	size_t n;

	T_EQ_INT(fpal_dmsg_build(buf, sizeof(buf), &n, &m), FPAL_OK);
	T_EQ_INT(fpal_dmsg_parse(buf, n, &m), FPAL_E_PROTO);
}

/* ---- build ------------------------------------------------------------- */

static void test_build_roundtrip_call(void) {
	uint8_t buf[256], body[64];
	struct fpal_dw w;
	struct fpal_dmsg in = { .type = FPAL_DMSG_CALL, .serial = 42,
				.path = "/net/reactivated/Fprint/Manager",
				.iface = "net.reactivated.Fprint.Manager",
				.member = "GetDefaultDevice",
				.dest = "net.reactivated.Fprint" };
	struct fpal_dmsg out;
	size_t n;

	fpal_dw_init(&w, body, sizeof(body));
	fpal_dw_str(&w, "hello");
	in.sig = "s";
	in.body = body;
	in.body_len = (uint32_t)w.len;

	T_EQ_INT(fpal_dmsg_build(buf, sizeof(buf), &n, &in), FPAL_OK);
	T_EQ_INT(fpal_dmsg_parse(buf, n, &out), FPAL_OK);
	T_EQ_INT(out.serial, 42);
	T_CHECK(!strcmp(out.member, "GetDefaultDevice"));
	T_CHECK(!strcmp(out.dest, "net.reactivated.Fprint"));
	T_CHECK(!strcmp(out.sig, "s"));
	T_EQ_INT(out.body_len, in.body_len);
	T_EQ_MEM(out.body, body, in.body_len);
}

static void test_build_roundtrip_error(void) {
	uint8_t buf[256];
	struct fpal_dmsg in = { .type = FPAL_DMSG_ERROR, .serial = 3,
				.reply_serial = 77,
				.errname = "org.freedesktop.DBus.Error.Failed",
				.dest = ":1.4" };
	struct fpal_dmsg out;
	size_t n;

	T_EQ_INT(fpal_dmsg_build(buf, sizeof(buf), &n, &in), FPAL_OK);
	T_EQ_INT(fpal_dmsg_parse(buf, n, &out), FPAL_OK);
	T_EQ_INT(out.reply_serial, 77);
	T_CHECK(!strcmp(out.errname, in.errname));
	T_CHECK(!strcmp(out.dest, ":1.4"));
}

static void test_build_overflow(void) {
	uint8_t buf[20];
	struct fpal_dmsg in = { .type = FPAL_DMSG_CALL, .serial = 1,
				.path = "/x", .member = "M" };
	size_t n;

	T_EQ_INT(fpal_dmsg_build(buf, sizeof(buf), &n, &in), FPAL_E_OVERFLOW);
}

int main(void) {
	T_RUN(test_alignment);
	T_RUN(test_string_and_signature);
	T_RUN(test_array_length);
	T_RUN(test_struct_array_pads_to_8);
	T_RUN(test_writer_overflow_is_sticky);
	T_RUN(test_reader_rejects_bad_strings);
	T_RUN(test_reader_rejects_nonzero_padding);
	T_RUN(test_parse_hello);
	T_RUN(test_parse_return_with_path);
	T_RUN(test_parse_property_dict);
	T_RUN(test_parse_error);
	T_RUN(test_total_len);
	T_RUN(test_rejects_damaged_messages);
	T_RUN(test_rejects_call_without_member);
	T_RUN(test_build_roundtrip_call);
	T_RUN(test_build_roundtrip_error);
	T_RUN(test_build_overflow);
	return T_DONE();
}
