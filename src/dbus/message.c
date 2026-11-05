// SPDX-License-Identifier: Apache-2.0
#include "dbus/message.h"

#include <string.h>

#include "core/bytes.h"
#include "dbus/marshal.h"
#include "fpal/config.h"

#define ENDIAN_LITTLE 'l'
#define PROTO_VERSION 1u

/* Header field codes. */
enum {
	F_PATH = 1,
	F_INTERFACE = 2,
	F_MEMBER = 3,
	F_ERROR_NAME = 4,
	F_REPLY_SERIAL = 5,
	F_DESTINATION = 6,
	F_SENDER = 7,
	F_SIGNATURE = 8,
	F_UNIX_FDS = 9,
};

static size_t pad8(size_t n) {
	return (n + 7) & ~(size_t)7;
}

int fpal_dmsg_frame_len(const uint8_t *hdr, uint64_t *total) {
	uint32_t body = fpal_get_le32(hdr + 4);
	uint32_t fields = fpal_get_le32(hdr + 12);

	if (hdr[0] != ENDIAN_LITTLE || hdr[3] != PROTO_VERSION ||
	    hdr[1] < FPAL_DMSG_CALL || hdr[1] > FPAL_DMSG_SIGNAL)
		return FPAL_E_PROTO;
	*total = FPAL_DMSG_FIXED + (uint64_t)pad8(fields) + body;
	return FPAL_OK;
}

int fpal_dmsg_total_len(const uint8_t *hdr, size_t *total) {
	uint64_t t;
	int err = fpal_dmsg_frame_len(hdr, &t);

	if (err)
		return err;
	if (t > FPAL_DBUS_MSG_MAX)
		return FPAL_E_OVERFLOW;
	*total = (size_t)t;
	return FPAL_OK;
}

static int set_str(const char **dst, struct fpal_dr *r) {
	if (*dst)
		return FPAL_E_PROTO; /* duplicate field */
	*dst = fpal_dr_str(r);
	return r->err;
}

static int set_sig(const char **dst, struct fpal_dr *r) {
	if (*dst)
		return FPAL_E_PROTO;
	*dst = fpal_dr_sig(r);
	return r->err;
}

static int parse_field(struct fpal_dr *r, struct fpal_dmsg *m) {
	uint8_t code;
	const char *vsig;

	fpal_dr_align(r, 8);
	code = fpal_dr_u8(r);
	vsig = fpal_dr_sig(r);
	if (r->err)
		return r->err;

	switch (code) {
	case F_PATH:
		return strcmp(vsig, "o") ? FPAL_E_PROTO : set_str(&m->path, r);
	case F_INTERFACE:
		return strcmp(vsig, "s") ? FPAL_E_PROTO : set_str(&m->iface, r);
	case F_MEMBER:
		return strcmp(vsig, "s") ? FPAL_E_PROTO : set_str(&m->member, r);
	case F_ERROR_NAME:
		return strcmp(vsig, "s") ? FPAL_E_PROTO
					 : set_str(&m->errname, r);
	case F_DESTINATION:
		return strcmp(vsig, "s") ? FPAL_E_PROTO : set_str(&m->dest, r);
	case F_SENDER:
		return strcmp(vsig, "s") ? FPAL_E_PROTO : set_str(&m->sender, r);
	case F_SIGNATURE:
		return strcmp(vsig, "g") ? FPAL_E_PROTO : set_sig(&m->sig, r);
	case F_REPLY_SERIAL:
		if (strcmp(vsig, "u"))
			return FPAL_E_PROTO;
		m->reply_serial = fpal_dr_u32(r);
		return r->err;
	case F_UNIX_FDS:
		/* We never negotiate fd passing, so this must not appear. */
		return FPAL_E_PROTO;
	default:
		/* Unknown fields must be ignored; skip the simple types. */
		if (!strcmp(vsig, "s") || !strcmp(vsig, "o"))
			(void)fpal_dr_str(r);
		else if (!strcmp(vsig, "g"))
			(void)fpal_dr_sig(r);
		else if (!strcmp(vsig, "u"))
			(void)fpal_dr_u32(r);
		else
			return FPAL_E_PROTO;
		return r->err;
	}
}

int fpal_dmsg_parse(const uint8_t *buf, size_t len, struct fpal_dmsg *m) {
	struct fpal_dr r;
	size_t total, end;
	uint32_t nfields;
	int rc;

	memset(m, 0, sizeof(*m));
	if (len < FPAL_DMSG_FIXED)
		return FPAL_E_PROTO;
	rc = fpal_dmsg_total_len(buf, &total);
	if (rc)
		return rc;
	if (total != len)
		return FPAL_E_PROTO;

	m->type = buf[1];
	m->flags = buf[2];
	m->body_len = fpal_get_le32(buf + 4);
	m->serial = fpal_get_le32(buf + 8);
	if (m->serial == 0)
		return FPAL_E_PROTO;

	fpal_dr_init(&r, buf, len);
	r.off = 12;
	nfields = fpal_dr_array_begin(&r, 8);
	if (r.err)
		return r.err;
	end = r.off + nfields;
	while (r.off < end) {
		rc = parse_field(&r, m);
		if (rc)
			return rc;
	}
	if (r.off != end)
		return FPAL_E_PROTO;
	fpal_dr_align(&r, 8);
	if (r.err || len - r.off != m->body_len)
		return FPAL_E_PROTO;
	m->body = buf + r.off;

	switch (m->type) {
	case FPAL_DMSG_CALL:
		if (!m->path || !m->member)
			return FPAL_E_PROTO;
		break;
	case FPAL_DMSG_SIGNAL:
		if (!m->path || !m->iface || !m->member)
			return FPAL_E_PROTO;
		break;
	case FPAL_DMSG_ERROR:
		if (!m->errname)
			return FPAL_E_PROTO;
		/* fall through */
	case FPAL_DMSG_RETURN:
		if (!m->reply_serial)
			return FPAL_E_PROTO;
		break;
	}
	if (m->body_len && (!m->sig || !*m->sig))
		return FPAL_E_PROTO;
	return FPAL_OK;
}

static void put_field_str(struct fpal_dw *w, uint8_t code, char type,
			  const char *v) {
	char sig[2] = { type, '\0' };

	if (!v)
		return;
	fpal_dw_align(w, 8);
	fpal_dw_u8(w, code);
	fpal_dw_variant(w, sig);
	if (type == 'g')
		fpal_dw_sig(w, v);
	else
		fpal_dw_str(w, v);
}

int fpal_dmsg_build(uint8_t *out, size_t cap, size_t *outlen,
		    const struct fpal_dmsg *m) {
	struct fpal_dw w;
	struct fpal_dw_arr arr;

	fpal_dw_init(&w, out, cap);
	fpal_dw_u8(&w, ENDIAN_LITTLE);
	fpal_dw_u8(&w, m->type);
	fpal_dw_u8(&w, m->flags);
	fpal_dw_u8(&w, PROTO_VERSION);
	fpal_dw_u32(&w, m->body_len);
	fpal_dw_u32(&w, m->serial);

	fpal_dw_array_begin(&w, 8, &arr);
	put_field_str(&w, F_PATH, 'o', m->path);
	put_field_str(&w, F_INTERFACE, 's', m->iface);
	put_field_str(&w, F_MEMBER, 's', m->member);
	put_field_str(&w, F_ERROR_NAME, 's', m->errname);
	if (m->reply_serial) {
		fpal_dw_align(&w, 8);
		fpal_dw_u8(&w, F_REPLY_SERIAL);
		fpal_dw_variant(&w, "u");
		fpal_dw_u32(&w, m->reply_serial);
	}
	put_field_str(&w, F_DESTINATION, 's', m->dest);
	if (m->sig && *m->sig)
		put_field_str(&w, F_SIGNATURE, 'g', m->sig);
	fpal_dw_array_end(&w, &arr);

	fpal_dw_align(&w, 8);
	if (!w.err && m->body_len > w.cap - w.len)
		w.err = FPAL_E_OVERFLOW;
	if (w.err)
		return w.err;
	if (m->body_len)
		memcpy(w.p + w.len, m->body, m->body_len);
	*outlen = w.len + m->body_len;
	return FPAL_OK;
}
