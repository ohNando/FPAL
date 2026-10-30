// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * Driver for EgisTec / LighTuning Match-on-Chip sensors.
 *
 * Every command is one bulk OUT frame followed by one bulk IN response.
 * The sensor keeps templates and does the matching itself; the host only
 * learns the 32-byte id of the template that matched.
 */
#include <stdio.h>
#include <string.h>

#include "core/driver.h"
#include "core/log.h"
#include "egis_moc_proto.h"

static const struct fpx_usb_id egis_moc_ids[] = {
	{ 0x1c7a, 0x0582, 0 },
	{ 0x1c7a, 0x0583, 0 },
	{ 0x1c7a, 0x0584, EGIS_FLAG_ENROLL_STAGES_20 },
	{ 0x1c7a, 0x0586, EGIS_FLAG_ENROLL_STAGES_20 },
	{ 0x1c7a, 0x0587, EGIS_FLAG_ENROLL_STAGES_20 },
	{ 0x1c7a, 0x0588, EGIS_FLAG_ENROLL_STAGES_20 },
	{ 0x1c7a, 0x05a1, EGIS_FLAG_CHECK_PREFIX_TYPE2 },
	{ 0, 0, 0 },
};

struct egis_priv {
	uint8_t tx[FPX_USB_TX_MAX];
	uint8_t rx[FPX_USB_RX_MAX];
	uint8_t payload[FPX_USB_TX_MAX];
	uint8_t irq[FPX_USB_IRQ_MAX];
	bool claimed;
};

/* One step of the vendor control sequence run at open time. */
struct egis_ctrl_step {
	uint8_t req_type;
	uint8_t req;
	uint16_t index;
	uint16_t len;
};

/* Copied from the Windows driver's init sequence; all device-to-host reads. */
static const struct egis_ctrl_step egis_init_seq[] = {
	{ FPX_USB_TYPE_VENDOR | FPX_USB_RECIP_DEVICE, 32, 4, 16 },
	{ FPX_USB_TYPE_VENDOR | FPX_USB_RECIP_DEVICE, 32, 4, 40 },
	{ FPX_USB_TYPE_STANDARD | FPX_USB_RECIP_DEVICE, 0, 0, 2 }, /* GET_STATUS */
	{ FPX_USB_TYPE_STANDARD | FPX_USB_RECIP_DEVICE, 0, 0, 2 }, /* GET_STATUS */
	{ FPX_USB_TYPE_VENDOR | FPX_USB_RECIP_DEVICE, 82, 0, 8 },
};

static struct egis_priv *priv(struct fpx_device *dev)
{
	return dev->priv;
}

/* Send one command payload and parse the response into @rsp. */
static int egis_exec(struct fpx_device *dev, const uint8_t *payload,
		     size_t plen, struct egis_rsp *rsp)
{
	struct egis_priv *p = priv(dev);
	const struct fpx_transport *tr = &dev->tr;
	size_t tx_len, rx_len = 0;
	int err;

	err = egis_frame(payload, plen, p->tx, sizeof(p->tx), &tx_len);
	if (err)
		return err;

	err = tr->ops->bulk_out(tr->ctx, EGIS_EP_OUT, p->tx, tx_len,
				FPX_CMD_TIMEOUT_MS);
	if (err)
		return err;

	err = tr->ops->bulk_in(tr->ctx, EGIS_EP_IN, p->rx, sizeof(p->rx),
			       &rx_len, FPX_CMD_TIMEOUT_MS);
	if (err)
		return err;

	err = egis_parse(p->rx, rx_len, rsp);
	if (err)
		return err;

	/* Confirmed on 1c7a:0587 fw 9050.6.4.67: responses carry a valid ck. */
	if (!rsp->checksum_ok) {
		fpx_warn("response checksum mismatch");
		return FPX_E_PROTO;
	}
	return FPX_OK;
}

static int egis_exec_cmd(struct fpx_device *dev, const struct egis_cmd *cmd,
			 struct egis_rsp *rsp)
{
	return egis_exec(dev, cmd->data, cmd->len, rsp);
}

/* Commands whose answer carries no data; only the status word is logged. */
static int egis_exec_simple(struct fpx_device *dev, const struct egis_cmd *cmd)
{
	struct egis_rsp rsp;
	int err = egis_exec_cmd(dev, cmd, &rsp);

	if (!err && rsp.sw != EGIS_SW_OK)
		fpx_dbg("command 0x%02x answered status %04x", cmd->data[5],
			rsp.sw);
	return err;
}

static int egis_read_fw_version(struct fpx_device *dev)
{
	struct egis_rsp rsp;
	size_t len;
	int err;

	err = egis_exec_cmd(dev, &egis_cmd_fw_version, &rsp);
	if (err)
		return err;
	if (rsp.sw != EGIS_SW_OK)
		return FPX_E_PROTO;

	len = rsp.body_len < sizeof(dev->fw_version) - 1 ?
		      rsp.body_len :
		      sizeof(dev->fw_version) - 1;
	memcpy(dev->fw_version, rsp.body, len);
	dev->fw_version[len] = '\0';

	/* Keep the string printable. */
	for (size_t i = 0; i < len; i++)
		if (dev->fw_version[i] < 0x20 || dev->fw_version[i] > 0x7e)
			dev->fw_version[i] = '.';
	return FPX_OK;
}

static int egis_open(struct fpx_device *dev)
{
	struct egis_priv *p = priv(dev);
	const struct fpx_transport *tr = &dev->tr;
	uint8_t buf[64];
	int err;

	/* Claim first so we never reset a device another process is using. */
	err = tr->ops->claim(tr->ctx, EGIS_IFACE);
	if (err)
		return err;
	tr->ops->release(tr->ctx, EGIS_IFACE);

	err = tr->ops->reset(tr->ctx);
	if (err)
		return err;

	err = tr->ops->claim(tr->ctx, EGIS_IFACE);
	if (err)
		return err;
	p->claimed = true;

	for (size_t i = 0; i < sizeof(egis_init_seq) / sizeof(egis_init_seq[0]);
	     i++) {
		const struct egis_ctrl_step *s = &egis_init_seq[i];

		err = tr->ops->control_in(tr->ctx, s->req_type, s->req, 0,
					  s->index, buf, s->len,
					  FPX_CONTROL_TIMEOUT_MS);
		if (err) {
			fpx_dbg("init control step %zu failed", i + 1);
			goto fail;
		}
	}

	err = egis_read_fw_version(dev);
	if (err)
		goto fail;

	fpx_dbg("firmware version %s", dev->fw_version);
	return FPX_OK;

fail:
	tr->ops->release(tr->ctx, EGIS_IFACE);
	p->claimed = false;
	return err;
}

static void egis_close(struct fpx_device *dev)
{
	struct egis_priv *p = priv(dev);

	if (p->claimed)
		dev->tr.ops->release(dev->tr.ctx, EGIS_IFACE);
	p->claimed = false;
}

static int egis_list(struct fpx_device *dev, struct fpx_template_id *out,
		     size_t cap, size_t *n)
{
	struct egis_rsp rsp;
	int err;

	err = egis_exec_cmd(dev, &egis_cmd_list, &rsp);
	if (err)
		return err;
	return egis_parse_list(&rsp, out, cap, n);
}

static int egis_wait_finger(struct fpx_device *dev, uint32_t timeout_ms)
{
	struct egis_priv *p = priv(dev);
	size_t got = 0;

	/* The sensor always returns one byte less than requested; not an error. */
	return dev->tr.ops->wait_interrupt(dev->tr.ctx, EGIS_EP_IRQ, p->irq,
					   sizeof(p->irq), &got, timeout_ms);
}

static bool id_in_list(const struct fpx_template_id *id,
		       const struct fpx_template_id *ids, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (memcmp(id->bytes, ids[i].bytes, FPX_TEMPLATE_ID_LEN) == 0)
			return true;
	return false;
}

static int egis_match(struct fpx_device *dev, const struct fpx_template_id *ids,
		      size_t n, struct fpx_template_id *match)
{
	struct egis_priv *p = priv(dev);
	struct egis_rsp rsp;
	size_t plen;
	int err;

	err = egis_build_check(ids, n, dev->id->flags, p->payload,
			       sizeof(p->payload), &plen);
	if (err)
		return err;

	err = egis_exec(dev, p->payload, plen, &rsp);
	if (err)
		return err;

	if (rsp.sw == EGIS_SW_NOMATCH)
		return FPX_E_NOMATCH;
	if (rsp.sw != EGIS_SW_OK)
		return FPX_E_PROTO;

	if (rsp.body_len < EGIS_IDENTIFY_ID_BODY_OFF + FPX_TEMPLATE_ID_LEN) {
		fpx_dbg("identify response body too short (%zu)",
			rsp.body_len);
		return FPX_E_PROTO;
	}
	memcpy(match->bytes, rsp.body + EGIS_IDENTIFY_ID_BODY_OFF,
	       FPX_TEMPLATE_ID_LEN);

	/*
	 * The id offset comes from reverse engineering. A matched id that is
	 * not on the sensor means the offset is wrong for this firmware;
	 * refuse rather than report a bogus match.
	 */
	if (!id_in_list(match, ids, n)) {
		fpx_warn("matched id is not stored on the sensor; "
			 "run with " FPX_ENV_TRACE "=1 and report the dump");
		return FPX_E_PROTO;
	}
	return FPX_OK;
}

static int egis_identify(struct fpx_device *dev, uint32_t timeout_ms,
			 struct fpx_template_id *match)
{
	struct fpx_template_id ids[FPX_MAX_TEMPLATES];
	size_t n = 0;
	int err, rerr;

	err = egis_list(dev, ids, FPX_MAX_TEMPLATES, &n);
	if (err)
		return err;
	if (n == 0)
		return FPX_E_NODATA;

	err = egis_exec_simple(dev, &egis_cmd_sensor_reset);
	if (err)
		return err;

	err = egis_exec_simple(dev, &egis_cmd_arm_identify);
	if (!err)
		err = egis_wait_finger(dev, timeout_ms);
	if (!err)
		err = egis_exec_simple(dev, &egis_cmd_sensor_check);
	if (!err)
		err = egis_match(dev, ids, n, match);

	/* Always leave the sensor idle, whatever happened above. */
	rerr = egis_exec_simple(dev, &egis_cmd_sensor_reset);
	if (rerr)
		fpx_dbg("final sensor reset failed: %s", fpx_strerror(rerr));

	return err;
}

const struct fpx_driver fpx_driver_egis_moc = {
	.name = "egis_moc",
	.ids = egis_moc_ids,
	.caps = FPX_CAP_MOC,
	.priv_size = sizeof(struct egis_priv),
	.open = egis_open,
	.close = egis_close,
	.list = egis_list,
	.identify = egis_identify,
	.enroll = NULL,
};
