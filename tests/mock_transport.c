// SPDX-License-Identifier: LGPL-2.1-or-later
#include <string.h>

#include "core/bytes.h"
#include "drivers/egis_moc/egis_moc_proto.h"
#include "fpx/types.h"
#include "mock_transport.h"

static int m_reset(void *ctx)
{
	struct mock *m = ctx;

	m->resets++;
	return FPX_OK;
}

static int m_claim(void *ctx, uint8_t iface)
{
	struct mock *m = ctx;

	(void)iface;
	m->claims++;
	return m->claim_rc;
}

static void m_release(void *ctx, uint8_t iface)
{
	struct mock *m = ctx;

	(void)iface;
	m->releases++;
}

static int m_control_in(void *ctx, uint8_t req_type, uint8_t req,
			uint16_t value, uint16_t index, uint8_t *buf,
			uint16_t len, uint32_t timeout_ms)
{
	struct mock *m = ctx;

	(void)req_type, (void)req, (void)value, (void)index, (void)timeout_ms;
	m->ctrls++;
	memset(buf, 0, len);
	return FPX_OK;
}

static int m_bulk_out(void *ctx, uint8_t ep, const uint8_t *buf, size_t len,
		      uint32_t timeout_ms)
{
	struct mock *m = ctx;

	(void)ep, (void)timeout_ms;
	if (m->n_out >= MOCK_MAX_FRAMES || len > sizeof(m->out[0].data))
		return FPX_E_IO;
	memcpy(m->out[m->n_out].data, buf, len);
	m->out[m->n_out].len = len;
	m->n_out++;
	return FPX_OK;
}

static int m_bulk_in(void *ctx, uint8_t ep, uint8_t *buf, size_t cap,
		     size_t *got, uint32_t timeout_ms)
{
	struct mock *m = ctx;
	const struct mock_frame *f;

	(void)ep, (void)timeout_ms;
	if (m->i_in >= m->n_in)
		return FPX_E_TIMEOUT; /* script exhausted */
	f = &m->in[m->i_in++];
	if (f->len > cap)
		return FPX_E_OVERFLOW;
	memcpy(buf, f->data, f->len);
	*got = f->len;
	return FPX_OK;
}

static int m_wait_interrupt(void *ctx, uint8_t ep, uint8_t *buf, size_t cap,
			    size_t *got, uint32_t timeout_ms)
{
	struct mock *m = ctx;

	(void)ep, (void)timeout_ms;
	m->irqs++;
	if (m->irq_rc)
		return m->irq_rc;
	memset(buf, 0, cap);
	*got = cap - 1; /* the real sensor is always one byte short */
	return FPX_OK;
}

static void m_cancel(void *ctx)
{
	struct mock *m = ctx;

	m->irq_rc = FPX_E_CANCELLED;
}

static void m_close(void *ctx)
{
	(void)ctx;
}

static const struct fpx_transport_ops mock_ops = {
	.reset = m_reset,
	.claim = m_claim,
	.release = m_release,
	.control_in = m_control_in,
	.bulk_out = m_bulk_out,
	.bulk_in = m_bulk_in,
	.wait_interrupt = m_wait_interrupt,
	.cancel = m_cancel,
	.close = m_close,
};

void mock_init(struct mock *m)
{
	memset(m, 0, sizeof(*m));
}

struct fpx_transport mock_transport(struct mock *m)
{
	return (struct fpx_transport){ .ops = &mock_ops, .ctx = m };
}

void mock_push_egis(struct mock *m, const uint8_t *body, size_t body_len,
		    uint16_t sw)
{
	static const uint8_t magic[EGIS_MAGIC_LEN] = { 'S', 'I', 'G', 'E',
						       0, 0, 0, 1 };
	struct mock_frame *f;

	if (m->n_in >= MOCK_MAX_FRAMES)
		return;
	f = &m->in[m->n_in++];
	memcpy(f->data, magic, sizeof(magic));
	fpx_put_be16(f->data + EGIS_MAGIC_LEN, 0);
	fpx_put_be32(f->data + EGIS_HDR_LEN, (uint32_t)(body_len + EGIS_SW_LEN));
	if (body_len)
		memcpy(f->data + EGIS_RSP_BODY_OFF, body, body_len);
	fpx_put_be16(f->data + EGIS_RSP_BODY_OFF + body_len, sw);
	f->len = EGIS_RSP_BODY_OFF + body_len + EGIS_SW_LEN;
	fpx_put_be16(f->data + EGIS_MAGIC_LEN, egis_checksum(f->data, f->len));
}

/* The library references the real backend; tests never open hardware. */
int fpx_usbdevfs_open(uint8_t busnum, uint8_t devnum,
		      struct fpx_transport *out)
{
	(void)busnum, (void)devnum, (void)out;
	return FPX_E_NODEV;
}
