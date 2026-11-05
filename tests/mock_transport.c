// SPDX-License-Identifier: Apache-2.0
#include <string.h>

#include "core/bytes.h"
#include "drivers/egis_moc/egis_moc_proto.h"
#include "fpal/types.h"
#include "mock_transport.h"

static int m_reset(void *ctx) {
	struct mock *m = ctx;

	m->resets++;
	return FPAL_OK;
}

static int m_claim(void *ctx, uint8_t iface) {
	struct mock *m = ctx;

	(void)iface;
	m->claims++;
	return m->claim_rc;
}

static void m_release(void *ctx, uint8_t iface) {
	struct mock *m = ctx;

	(void)iface;
	m->releases++;
}

static int m_control_in(void *ctx, uint8_t req_type, uint8_t req,
			uint16_t value, uint16_t index, uint8_t *buf,
			uint16_t len, uint32_t timeout_ms) {
	struct mock *m = ctx;

	(void)req_type, (void)req, (void)value, (void)index, (void)timeout_ms;
	m->ctrls++;
	memset(buf, 0, len);
	if (m->i_ctrl_in < m->n_ctrl_in) {
		const struct mock_frame *f = &m->ctrl_in[m->i_ctrl_in++];

		memcpy(buf, f->data, f->len < len ? f->len : len);
	}
	return FPAL_OK;
}

static int m_control_out(void *ctx, uint8_t req_type, uint8_t req,
			 uint16_t value, uint16_t index, const uint8_t *buf,
			 uint16_t len, uint32_t timeout_ms) {
	struct mock *m = ctx;
	struct mock_ctrl *c;

	(void)timeout_ms;
	if (m->n_ctrl_out >= MOCK_MAX_CTRL || len > MOCK_CTRL_DATA)
		return FPAL_E_IO;
	c = &m->ctrl_out[m->n_ctrl_out++];
	c->req_type = req_type;
	c->req = req;
	c->value = value;
	c->index = index;
	c->len = len;
	if (len)
		memcpy(c->data, buf, len);
	return m->ctrl_out_rc;
}

static int m_wait_bulk_in(void *ctx, uint8_t ep, uint8_t *buf, size_t cap,
			  size_t *got, uint32_t timeout_ms) {
	struct mock *m = ctx;
	const struct mock_frame *f;

	(void)ep, (void)timeout_ms;
	m->bulk_waits++;
	if (m->irq_rc == FPAL_E_CANCELLED)
		return FPAL_E_CANCELLED;
	if (m->i_wait_in >= m->n_wait_in)
		return m->bulk_wait_rc ? m->bulk_wait_rc : FPAL_E_TIMEOUT;
	f = &m->wait_in[m->i_wait_in++];
	if (f->len > cap)
		return FPAL_E_OVERFLOW;
	memcpy(buf, f->data, f->len);
	*got = f->len;
	return FPAL_OK;
}

static int m_bulk_out(void *ctx, uint8_t ep, const uint8_t *buf, size_t len,
		      uint32_t timeout_ms) {
	struct mock *m = ctx;

	(void)ep, (void)timeout_ms;
	if (m->n_out >= MOCK_MAX_FRAMES || len > sizeof(m->out[0].data))
		return FPAL_E_IO;
	memcpy(m->out[m->n_out].data, buf, len);
	m->out[m->n_out].len = len;
	m->n_out++;
	return FPAL_OK;
}

static int m_bulk_in(void *ctx, uint8_t ep, uint8_t *buf, size_t cap,
		     size_t *got, uint32_t timeout_ms) {
	struct mock *m = ctx;
	const struct mock_frame *f;

	(void)ep, (void)timeout_ms;
	if (m->i_in >= m->n_in)
		return FPAL_E_TIMEOUT; /* script exhausted */
	f = &m->in[m->i_in++];
	if (f->len > cap)
		return FPAL_E_OVERFLOW;
	memcpy(buf, f->data, f->len);
	*got = f->len;
	return FPAL_OK;
}

static int m_wait_interrupt(void *ctx, uint8_t ep, uint8_t *buf, size_t cap,
			    size_t *got, uint32_t timeout_ms) {
	struct mock *m = ctx;

	(void)ep, (void)timeout_ms;
	m->irqs++;
	if (m->irq_fail_at && m->irqs == m->irq_fail_at)
		return m->irq_fail_rc;
	if (m->irq_rc)
		return m->irq_rc;
	memset(buf, 0, cap);
	*got = cap - 1; /* the real sensor is always one byte short */
	return FPAL_OK;
}

static void m_cancel(void *ctx) {
	struct mock *m = ctx;

	m->irq_rc = FPAL_E_CANCELLED;
}

static void m_close(void *ctx) {
	(void)ctx;
}

static const struct fpal_transport_ops mock_ops = {
	.reset = m_reset,
	.claim = m_claim,
	.release = m_release,
	.control_in = m_control_in,
	.bulk_out = m_bulk_out,
	.bulk_in = m_bulk_in,
	.control_out = m_control_out,
	.wait_bulk_in = m_wait_bulk_in,
	.wait_interrupt = m_wait_interrupt,
	.cancel = m_cancel,
	.close = m_close,
};

void mock_init(struct mock *m) {
	memset(m, 0, sizeof(*m));
}

struct fpal_transport mock_transport(struct mock *m) {
	return (struct fpal_transport){ .ops = &mock_ops, .ctx = m };
}

void mock_push_ctrl_in(struct mock *m, const void *data, size_t len) {
	if (m->n_ctrl_in >= MOCK_MAX_CTRL || len > MOCK_FRAME_MAX)
		return;
	memcpy(m->ctrl_in[m->n_ctrl_in].data, data, len);
	m->ctrl_in[m->n_ctrl_in++].len = len;
}

void mock_push_wait_in(struct mock *m, const void *data, size_t len) {
	if (m->n_wait_in >= MOCK_MAX_CTRL || len > MOCK_FRAME_MAX)
		return;
	memcpy(m->wait_in[m->n_wait_in].data, data, len);
	m->wait_in[m->n_wait_in++].len = len;
}

void mock_push_egis(struct mock *m, const uint8_t *body, size_t body_len,
		    uint16_t sw) {
	static const uint8_t magic[EGIS_MAGIC_LEN] = { 'S', 'I', 'G', 'E',
						       0, 0, 0, 1 };
	struct mock_frame *f;

	if (m->n_in >= MOCK_MAX_FRAMES ||
	    EGIS_RSP_BODY_OFF + body_len + EGIS_SW_LEN > MOCK_FRAME_MAX)
		return;
	f = &m->in[m->n_in++];
	memcpy(f->data, magic, sizeof(magic));
	fpal_put_be16(f->data + EGIS_MAGIC_LEN, 0);
	fpal_put_be32(f->data + EGIS_HDR_LEN, (uint32_t)(body_len + EGIS_SW_LEN));
	if (body_len)
		memcpy(f->data + EGIS_RSP_BODY_OFF, body, body_len);
	fpal_put_be16(f->data + EGIS_RSP_BODY_OFF + body_len, sw);
	f->len = EGIS_RSP_BODY_OFF + body_len + EGIS_SW_LEN;
	fpal_put_be16(f->data + EGIS_MAGIC_LEN, egis_checksum(f->data, f->len));
}

/* The library references the real backend; tests never open hardware. */
int fpal_usbdevfs_open(uint8_t busnum, uint8_t devnum,
		      struct fpal_transport *out) {
	(void)busnum, (void)devnum, (void)out;
	return FPAL_E_NODEV;
}
