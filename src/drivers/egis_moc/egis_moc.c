// SPDX-License-Identifier: Apache-2.0
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

static const struct fpal_usb_id egis_moc_ids[] = {
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
	uint8_t tx[FPAL_USB_TX_MAX];
	uint8_t rx[FPAL_USB_RX_MAX];
	uint8_t payload[FPAL_USB_TX_MAX];
	uint8_t irq[FPAL_USB_IRQ_MAX];
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
	{ FPAL_USB_TYPE_VENDOR | FPAL_USB_RECIP_DEVICE, 32, 4, 16 },
	{ FPAL_USB_TYPE_VENDOR | FPAL_USB_RECIP_DEVICE, 32, 4, 40 },
	{ FPAL_USB_TYPE_STANDARD | FPAL_USB_RECIP_DEVICE, 0, 0, 2 }, /* GET_STATUS */
	{ FPAL_USB_TYPE_STANDARD | FPAL_USB_RECIP_DEVICE, 0, 0, 2 }, /* GET_STATUS */
	{ FPAL_USB_TYPE_VENDOR | FPAL_USB_RECIP_DEVICE, 82, 0, 8 },
};

static struct egis_priv *priv(struct fpal_device *dev) {
	return dev->priv;
}

/* Send one command payload and parse the response into @rsp. */
static int egis_exec(struct fpal_device *dev, const uint8_t *payload,
		     size_t plen, struct egis_rsp *rsp) {
	struct egis_priv *p = priv(dev);
	const struct fpal_transport *tr = &dev->tr;
	size_t tx_len, rx_len = 0;
	int err;

	err = egis_frame(payload, plen, p->tx, sizeof(p->tx), &tx_len);
	if (err)
		return err;

	err = tr->ops->bulk_out(tr->ctx, EGIS_EP_OUT, p->tx, tx_len,
				FPAL_CMD_TIMEOUT_MS);
	if (err)
		return err;

	err = tr->ops->bulk_in(tr->ctx, EGIS_EP_IN, p->rx, sizeof(p->rx),
			       &rx_len, FPAL_CMD_TIMEOUT_MS);
	if (err)
		return err;

	err = egis_parse(p->rx, rx_len, rsp);
	if (err)
		return err;

	/* Confirmed on 1c7a:0587 fw 9050.6.4.67: responses carry a valid ck. */
	if (!rsp->checksum_ok) {
		fpal_warn("response checksum mismatch");
		return FPAL_E_PROTO;
	}
	return FPAL_OK;
}

static int egis_exec_cmd(struct fpal_device *dev, const struct egis_cmd *cmd,
			 struct egis_rsp *rsp) {
	return egis_exec(dev, cmd->data, cmd->len, rsp);
}

/* Commands whose answer carries no data; only the status word is logged. */
static int egis_exec_simple(struct fpal_device *dev, const struct egis_cmd *cmd) {
	struct egis_rsp rsp;
	int err = egis_exec_cmd(dev, cmd, &rsp);

	if (!err && rsp.sw != EGIS_SW_OK)
		fpal_dbg("command 0x%02x answered status %04x", cmd->data[5],
			rsp.sw);
	return err;
}

static int egis_read_fw_version(struct fpal_device *dev) {
	struct egis_rsp rsp;
	size_t len;
	int err;

	err = egis_exec_cmd(dev, &egis_cmd_fw_version, &rsp);
	if (err)
		return err;
	if (rsp.sw != EGIS_SW_OK)
		return FPAL_E_PROTO;

	len = rsp.body_len < sizeof(dev->fw_version) - 1 ?
		      rsp.body_len :
		      sizeof(dev->fw_version) - 1;
	memcpy(dev->fw_version, rsp.body, len);
	dev->fw_version[len] = '\0';

	/* Keep the string printable. */
	for (size_t i = 0; i < len; i++)
		if (dev->fw_version[i] < 0x20 || dev->fw_version[i] > 0x7e)
			dev->fw_version[i] = '.';
	return FPAL_OK;
}

static int egis_open(struct fpal_device *dev) {
	struct egis_priv *p = priv(dev);
	const struct fpal_transport *tr = &dev->tr;
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
					  FPAL_CONTROL_TIMEOUT_MS);
		if (err) {
			fpal_dbg("init control step %zu failed", i + 1);
			goto fail;
		}
	}

	err = egis_read_fw_version(dev);
	if (err)
		goto fail;

	fpal_dbg("firmware version %s", dev->fw_version);
	return FPAL_OK;

fail:
	tr->ops->release(tr->ctx, EGIS_IFACE);
	p->claimed = false;
	return err;
}

static void egis_close(struct fpal_device *dev) {
	struct egis_priv *p = priv(dev);

	if (p->claimed)
		dev->tr.ops->release(dev->tr.ctx, EGIS_IFACE);
	p->claimed = false;
}

static int egis_list(struct fpal_device *dev, struct fpal_template_id *out,
		     size_t cap, size_t *n) {
	struct egis_rsp rsp;
	int err;

	err = egis_exec_cmd(dev, &egis_cmd_list, &rsp);
	if (err)
		return err;
	return egis_parse_list(&rsp, out, cap, n);
}

static int egis_wait_finger(struct fpal_device *dev, uint32_t timeout_ms) {
	struct egis_priv *p = priv(dev);
	size_t got = 0;

	/* The sensor always returns one byte less than requested; not an error. */
	return dev->tr.ops->wait_interrupt(dev->tr.ctx, EGIS_EP_IRQ, p->irq,
					   sizeof(p->irq), &got, timeout_ms);
}

static bool id_in_list(const struct fpal_template_id *id,
		       const struct fpal_template_id *ids, size_t n) {
	for (size_t i = 0; i < n; i++)
		if (memcmp(id->bytes, ids[i].bytes, FPAL_TEMPLATE_ID_LEN) == 0)
			return true;
	return false;
}

static int egis_match(struct fpal_device *dev, const struct fpal_template_id *ids,
		      size_t n, struct fpal_template_id *match) {
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
		return FPAL_E_NOMATCH;
	if (rsp.sw != EGIS_SW_OK)
		return FPAL_E_PROTO;

	if (rsp.body_len < EGIS_IDENTIFY_ID_BODY_OFF + FPAL_TEMPLATE_ID_LEN) {
		fpal_dbg("identify response body too short (%zu)",
			rsp.body_len);
		return FPAL_E_PROTO;
	}
	memcpy(match->bytes, rsp.body + EGIS_IDENTIFY_ID_BODY_OFF,
	       FPAL_TEMPLATE_ID_LEN);

	/*
	 * The id offset comes from reverse engineering. A matched id that is
	 * not on the sensor means the offset is wrong for this firmware;
	 * refuse rather than report a bogus match.
	 */
	if (!id_in_list(match, ids, n)) {
		fpal_warn("matched id is not stored on the sensor; "
			 "run with " FPAL_ENV_TRACE "=1 and report the dump");
		return FPAL_E_PROTO;
	}
	return FPAL_OK;
}

static int egis_identify(struct fpal_device *dev, uint32_t timeout_ms,
			 struct fpal_template_id *match) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	size_t n = 0;
	int err, rerr;

	err = egis_list(dev, ids, FPAL_MAX_TEMPLATES, &n);
	if (err)
		return err;
	if (n == 0)
		return FPAL_E_NODATA;

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
		fpal_dbg("final sensor reset failed: %s", fpal_strerror(rerr));

	return err;
}

#ifdef FPAL_ENABLE_ENROLL
/*
 * Enrolling, as the Windows driver does it. Everything before the commit
 * (enrol mode, 20 scans) lives in the sensor's working memory; only
 * egis_build_commit() stores a template. Nothing here deletes anything.
 */

static void emit(fpal_enroll_cb cb, void *ud, enum fpal_enroll_event ev,
		 uint8_t stage, uint8_t total) {
	if (cb)
		cb(ev, stage, total, ud);
}

/* A finger the sensor already knows must not be enrolled a second time. */
static int egis_enroll_check_new(struct fpal_device *dev,
				 const struct fpal_template_id *ids, size_t n) {
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
		return FPAL_OK;
	return rsp.sw == EGIS_SW_OK ? FPAL_E_EXISTS : FPAL_E_PROTO;
}

/* The scan loop: @stages accepted scans, retries do not count. */
static int egis_enroll_scans(struct fpal_device *dev, uint8_t stages,
			     fpal_enroll_cb cb, void *ud) {
	unsigned done = 0, attempts = 0;

	while (done < stages) {
		struct egis_rsp rsp;
		int err;

		if (++attempts > stages + FPAL_ENROLL_MAX_RETRIES)
			return FPAL_E_TIMEOUT;

		emit(cb, ud, FPAL_ENROLL_TOUCH, (uint8_t)done, stages);
		err = egis_exec_simple(dev, &egis_cmd_sensor_reset);
		if (!err)
			err = egis_exec_simple(dev, &egis_cmd_start_capture);
		if (!err)
			err = egis_wait_finger(dev, FPAL_FINGER_WAIT_TIMEOUT_MS);
		if (!err)
			err = egis_exec_cmd(dev, &egis_cmd_read_capture, &rsp);
		if (err)
			return err;

		switch (egis_classify_capture(&rsp)) {
		case EGIS_CAPTURE_OK:
			done++;
			emit(cb, ud, FPAL_ENROLL_STAGE_DONE, (uint8_t)done,
			     stages);
			break;
		case EGIS_CAPTURE_CENTER:
			emit(cb, ud, FPAL_ENROLL_RETRY_CENTER, (uint8_t)done,
			     stages);
			break;
		case EGIS_CAPTURE_DIRTY:
			emit(cb, ud, FPAL_ENROLL_RETRY_DIRTY, (uint8_t)done,
			     stages);
			break;
		default:
			fpal_dbg("capture answered status %04x, body %zu bytes",
				rsp.sw, rsp.body_len);
			emit(cb, ud, FPAL_ENROLL_RETRY, (uint8_t)done, stages);
			break;
		}
	}
	return FPAL_OK;
}

/* The one chip write: store the template under @id. */
static int egis_enroll_commit(struct fpal_device *dev,
			      const struct fpal_template_id *id) {
	struct egis_priv *p = priv(dev);
	struct egis_rsp rsp;
	size_t plen;
	int err;

	err = egis_exec_simple(dev, &egis_cmd_commit_start);
	if (err)
		return err;
	err = egis_build_commit(id, p->payload, sizeof(p->payload), &plen);
	if (err)
		return err;
	err = egis_exec(dev, p->payload, plen, &rsp);
	if (err)
		return err;
	/*
	 * The status word of the store command was never captured; the list
	 * read back afterwards decides whether it worked.
	 */
	if (rsp.sw != EGIS_SW_OK)
		fpal_warn("store command answered status %04x", rsp.sw);
	return FPAL_OK;
}

static int egis_enroll(struct fpal_device *dev,
		       const struct fpal_template_id *new_id, uint32_t flags,
		       fpal_enroll_cb cb, void *ud) {
	struct fpal_template_id ids[FPAL_MAX_TEMPLATES];
	const uint8_t stages =
		(dev->id->flags & EGIS_FLAG_ENROLL_STAGES_20) ? 20 : 10;
	const bool dry_run = flags & FPAL_ENROLL_DRY_RUN;
	size_t n = 0;
	int err, rerr;

	/* Read only: refuse before the sensor is put into enrol mode. */
	err = egis_list(dev, ids, FPAL_MAX_TEMPLATES, &n);
	if (err)
		return err;
	if (n >= FPAL_MAX_TEMPLATES)
		return FPAL_E_FULL;
	if (id_in_list(new_id, ids, n))
		return FPAL_E_EXISTS;

	err = egis_exec_simple(dev, &egis_cmd_sensor_reset);
	if (!err)
		err = egis_exec_simple(dev, &egis_cmd_arm_enroll);
	if (!err) {
		emit(cb, ud, FPAL_ENROLL_CHECK_TOUCH, 0, stages);
		err = egis_wait_finger(dev, FPAL_FINGER_WAIT_TIMEOUT_MS);
	}
	if (!err)
		err = egis_exec_simple(dev, &egis_cmd_sensor_check);
	if (!err)
		err = egis_enroll_check_new(dev, ids, n);
	if (!err)
		err = egis_exec_simple(dev, &egis_cmd_enroll_start);
	if (!err)
		err = egis_enroll_scans(dev, stages, cb, ud);
	if (!err && !dry_run)
		err = egis_enroll_commit(dev, new_id);

	/* Leave the sensor idle whatever happened; an unfinished enrol is dropped. */
	rerr = egis_exec_simple(dev, &egis_cmd_sensor_reset);
	if (rerr)
		fpal_dbg("final sensor reset failed: %s", fpal_strerror(rerr));
	if (err || dry_run)
		return err;

	/* Trust the sensor's own list, not the status word. */
	err = egis_list(dev, ids, FPAL_MAX_TEMPLATES, &n);
	if (err)
		return err;
	return id_in_list(new_id, ids, n) ? FPAL_OK : FPAL_E_PROTO;
}

/*
 * Delete one template. The command is the one the Windows driver sends; the
 * answer to it is a bare success status, so the sensor's list is read back
 * to make sure it removed exactly what was asked for.
 */
static int egis_delete(struct fpal_device *dev,
		       const struct fpal_template_id *id) {
	struct fpal_template_id before[FPAL_MAX_TEMPLATES];
	struct fpal_template_id after[FPAL_MAX_TEMPLATES];
	struct egis_priv *p = priv(dev);
	size_t n_before = 0, n_after = 0, plen;
	struct egis_rsp rsp;
	int err;

	err = egis_list(dev, before, FPAL_MAX_TEMPLATES, &n_before);
	if (err)
		return err;
	if (!id_in_list(id, before, n_before))
		return FPAL_E_NODATA;

	err = egis_build_delete(id, p->payload, sizeof(p->payload), &plen);
	if (err)
		return err;
	err = egis_exec(dev, p->payload, plen, &rsp);
	if (err)
		return err;
	if (rsp.sw != EGIS_SW_OK || rsp.body_len != 0) {
		fpal_warn("delete answered status %04x", rsp.sw);
		return FPAL_E_PROTO;
	}

	err = egis_list(dev, after, FPAL_MAX_TEMPLATES, &n_after);
	if (err)
		return err;
	/* exactly @id gone, everything else untouched */
	if (n_after != n_before - 1 || id_in_list(id, after, n_after))
		return FPAL_E_PROTO;
	for (size_t i = 0; i < n_before; i++) {
		if (memcmp(before[i].bytes, id->bytes, FPAL_TEMPLATE_ID_LEN) &&
		    !id_in_list(&before[i], after, n_after))
			return FPAL_E_PROTO;
	}
	return FPAL_OK;
}
#endif

const struct fpal_driver fpal_driver_egis_moc = {
	.name = "egis_moc",
	.ids = egis_moc_ids,
#ifdef FPAL_ENABLE_ENROLL
	.caps = FPAL_CAP_MOC | FPAL_CAP_ENROLL,
#else
	.caps = FPAL_CAP_MOC,
#endif
	.priv_size = sizeof(struct egis_priv),
	.open = egis_open,
	.close = egis_close,
	.list = egis_list,
	.identify = egis_identify,
#ifdef FPAL_ENABLE_ENROLL
	.enroll = egis_enroll,
	.delete_template = egis_delete,
#else
	.enroll = NULL,
	.delete_template = NULL,
#endif
};
