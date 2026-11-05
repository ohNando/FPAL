// SPDX-License-Identifier: Apache-2.0
/* See fake_moc.h: a made-up sensor that exercises the whole driver API. */
#include <string.h>

#include "core/log.h"
#include "fake_moc.h"

const struct fpal_usb_id fake_moc_usb_id = { FAKE_VID, FAKE_PID, 0 };

static const struct fpal_usb_id fake_ids[] = {
	{ FAKE_VID, FAKE_PID, 0 },
	{ 0, 0, 0 },
};

/* ---- the driver ---------------------------------------------------------- */

struct fake_priv {
	uint8_t rx[2 + FAKE_MAX_TEMPLATES * FAKE_ID_LEN];
	bool claimed;
};

static struct fake_priv *priv(struct fpal_device *dev) {
	return dev->priv;
}

/* Send a command, read the answer; byte 0 of the answer is a status. */
static int fake_exec(struct fpal_device *dev, const uint8_t *cmd, size_t len,
		     size_t *rx_len) {
	struct fake_priv *p = priv(dev);
	const struct fpal_transport *tr = &dev->tr;
	int err;

	err = tr->ops->bulk_out(tr->ctx, FAKE_EP_OUT, cmd, len, FPAL_CMD_TIMEOUT_MS);
	if (err)
		return err;
	err = tr->ops->bulk_in(tr->ctx, FAKE_EP_IN, p->rx, sizeof(p->rx), rx_len,
			       FPAL_CMD_TIMEOUT_MS);
	if (err)
		return err;
	return (*rx_len >= 1 && p->rx[0] == 0) ? FPAL_OK : FPAL_E_PROTO;
}

static int fake_simple(struct fpal_device *dev, uint8_t cmd) {
	size_t got;

	return fake_exec(dev, &cmd, 1, &got);
}

static int fake_open(struct fpal_device *dev) {
	static const uint8_t vendor = FPAL_USB_TYPE_VENDOR | FPAL_USB_RECIP_DEVICE;
	const struct fpal_transport *tr = &dev->tr;
	uint8_t ver[4];
	int err;

	err = tr->ops->claim(tr->ctx, FAKE_IFACE);
	if (err)
		return err;
	priv(dev)->claimed = true;

	/* The chip needs to be switched on before it answers (control OUT). */
	err = tr->ops->control_out(tr->ctx, vendor, 0x01, 1, 0, NULL, 0,
				   FPAL_CONTROL_TIMEOUT_MS);
	if (!err)
		err = tr->ops->control_in(tr->ctx, vendor, 0x02, 0, 0, ver, sizeof(ver),
					  FPAL_CONTROL_TIMEOUT_MS);
	if (err) {
		tr->ops->release(tr->ctx, FAKE_IFACE);
		priv(dev)->claimed = false;
		return err;
	}
	memcpy(dev->fw_version, ver, sizeof(ver));
	dev->fw_version[sizeof(ver)] = '\0';
	fpal_text_sanitize(dev->fw_version);
	return FPAL_OK;
}

static void fake_close(struct fpal_device *dev) {
	if (priv(dev)->claimed)
		dev->tr.ops->release(dev->tr.ctx, FAKE_IFACE);
	priv(dev)->claimed = false;
}

static int fake_list(struct fpal_device *dev, struct fpal_template_id *out,
		     size_t cap, size_t *n) {
	struct fake_priv *p = priv(dev);
	size_t got, count;
	int err;

	err = fake_exec(dev, (const uint8_t[]){ FAKE_CMD_LIST }, 1, &got);
	if (err)
		return err;
	if (got < 2)
		return FPAL_E_PROTO;
	count = p->rx[1];
	if (got != 2 + count * FAKE_ID_LEN || count > FAKE_MAX_TEMPLATES)
		return FPAL_E_PROTO;
	if (count > cap)
		return FPAL_E_OVERFLOW;
	for (size_t i = 0; i < count; i++)
		(void)fpal_id_set(&out[i], p->rx + 2 + i * FAKE_ID_LEN, FAKE_ID_LEN);
	*n = count;
	return FPAL_OK;
}

/* Wait for the next event on the bulk event pipe. */
static int fake_event(struct fpal_device *dev, uint8_t *buf, size_t cap,
		      size_t *got, uint32_t timeout_ms) {
	return dev->tr.ops->wait_bulk_in(dev->tr.ctx, FAKE_EP_EVENT, buf, cap, got,
					 timeout_ms);
}

static int fake_identify(struct fpal_device *dev, uint32_t timeout_ms,
			 struct fpal_template_id *match) {
	struct fpal_template_id ids[FAKE_MAX_TEMPLATES];
	uint8_t evt[1 + FAKE_ID_LEN];
	size_t n = 0, got = 0;
	int err, rerr;

	err = fake_list(dev, ids, FAKE_MAX_TEMPLATES, &n);
	if (err)
		return err;
	if (n == 0)
		return FPAL_E_NODATA;

	err = fake_simple(dev, FAKE_CMD_ARM_IDENTIFY);
	if (!err)
		err = fake_event(dev, evt, sizeof(evt), &got, timeout_ms);
	if (!err && evt[0] == FAKE_EVT_NOMATCH) {
		err = FPAL_E_NOMATCH;
	} else if (!err) {
		err = FPAL_E_PROTO;
		if (evt[0] == FAKE_EVT_MATCH && got == sizeof(evt)) {
			(void)fpal_id_set(match, evt + 1, FAKE_ID_LEN);
			/* never report an id the chip does not hold */
			err = FPAL_E_PROTO;
			for (size_t i = 0; i < n; i++)
				if (fpal_id_equal(match, &ids[i]))
					err = FPAL_OK;
		}
	}

	rerr = fake_simple(dev, FAKE_CMD_RESET);
	if (rerr)
		fpal_dbg("final reset failed: %s", fpal_strerror(rerr));
	return err;
}

#ifdef FPAL_ENABLE_ENROLL
static int fake_enroll(struct fpal_device *dev, const struct fpal_template_id *new_id,
		       const struct fpal_template_id *existing, size_t n_existing,
		       uint32_t flags, fpal_enroll_cb cb, void *ud) {
	uint8_t evt[1 + FAKE_ID_LEN], cmd[1 + FAKE_ID_LEN];
	size_t got = 0;
	int err, rerr;

	(void)existing, (void)n_existing; /* the chip spots duplicates itself */

	err = fake_simple(dev, FAKE_CMD_ENROLL_BEGIN);
	for (unsigned stage = 0; !err && stage < FAKE_ENROLL_STAGES; stage++) {
		if (cb)
			cb(FPAL_ENROLL_TOUCH, (uint8_t)stage, FAKE_ENROLL_STAGES, ud);
		err = fake_event(dev, evt, sizeof(evt), &got, FPAL_FINGER_WAIT_TIMEOUT_MS);
		if (err)
			break;
		if (evt[0] == FAKE_EVT_DUPLICATE)
			err = FPAL_E_EXISTS;
		else if (evt[0] != FAKE_EVT_STAGE)
			err = FPAL_E_PROTO;
		else if (cb)
			cb(FPAL_ENROLL_STAGE_DONE, (uint8_t)(stage + 1),
			   FAKE_ENROLL_STAGES, ud);
	}
	if (!err && !(flags & FPAL_ENROLL_DRY_RUN)) {
		size_t rx;

		cmd[0] = FAKE_CMD_STORE;
		memcpy(cmd + 1, fpal_id_data(new_id), FAKE_ID_LEN);
		err = fake_exec(dev, cmd, sizeof(cmd), &rx);
	}

	rerr = fake_simple(dev, FAKE_CMD_RESET);
	if (rerr)
		fpal_dbg("final reset failed: %s", fpal_strerror(rerr));
	return err;
}

static int fake_delete(struct fpal_device *dev, const struct fpal_template_id *id) {
	uint8_t cmd[1 + FAKE_ID_LEN];
	size_t rx;

	cmd[0] = FAKE_CMD_DELETE;
	memcpy(cmd + 1, fpal_id_data(id), FAKE_ID_LEN);
	return fake_exec(dev, cmd, sizeof(cmd), &rx);
}
#endif

const struct fpal_driver fpal_driver_fake_moc = {
	.name = "fake_moc",
	.ids = fake_ids,
	.caps = FPAL_CAP_MOC,
	.priv_size = sizeof(struct fake_priv),
	.max_templates = FAKE_MAX_TEMPLATES,
	.new_id_len = FAKE_ID_LEN,
	.open = fake_open,
	.close = fake_close,
	.list = fake_list,
	.identify = fake_identify,
#ifdef FPAL_ENABLE_ENROLL
	.enroll = fake_enroll,
	.delete_template = fake_delete,
#endif
};

/* ---- the simulated chip -------------------------------------------------- */

static int c_reset(void *ctx) {
	((struct fake_chip *)ctx)->resets++;
	return FPAL_OK;
}

static int c_claim(void *ctx, uint8_t iface) {
	(void)iface;
	((struct fake_chip *)ctx)->claims++;
	return FPAL_OK;
}

static void c_release(void *ctx, uint8_t iface) {
	(void)iface;
	((struct fake_chip *)ctx)->releases++;
}

static int c_control_in(void *ctx, uint8_t req_type, uint8_t req, uint16_t value,
			uint16_t index, uint8_t *buf, uint16_t len,
			uint32_t timeout_ms) {
	(void)ctx, (void)req_type, (void)value, (void)index, (void)timeout_ms;
	memset(buf, 0, len);
	if (req == 0x02 && len >= 4)
		memcpy(buf, "FK01", 4);
	return FPAL_OK;
}

static int c_control_out(void *ctx, uint8_t req_type, uint8_t req, uint16_t value,
			 uint16_t index, const uint8_t *buf, uint16_t len,
			 uint32_t timeout_ms) {
	(void)req_type, (void)index, (void)buf, (void)len, (void)timeout_ms;
	((struct fake_chip *)ctx)->ctrl_outs++;
	/* anything but "power on" is refused */
	return (req == 0x01 && value == 1) ? FPAL_OK : FPAL_E_IO;
}

static int find(const struct fake_chip *c, const uint8_t *id) {
	for (size_t i = 0; i < c->n; i++)
		if (!memcmp(c->ids[i], id, FAKE_ID_LEN))
			return (int)i;
	return -1;
}

static void remove_at(struct fake_chip *c, size_t i) {
	memmove(c->ids[i], c->ids[i + 1], (c->n - i - 1) * FAKE_ID_LEN);
	c->n--;
}

static int c_bulk_out(void *ctx, uint8_t ep, const uint8_t *buf, size_t len,
		      uint32_t timeout_ms) {
	struct fake_chip *c = ctx;
	int i;

	(void)timeout_ms;
	if (ep != FAKE_EP_OUT || len < 1)
		return FPAL_E_IO;
	if (c->n_cmds < FAKE_LOG_MAX)
		c->cmds[c->n_cmds++] = buf[0];
	c->rsp[0] = 0;
	c->rsp_len = 1;
	c->rsp_ready = true;
	if (c->garbage) {
		c->rsp[0] = 0xee;
		return FPAL_OK;
	}

	switch (buf[0]) {
	case FAKE_CMD_LIST:
		c->rsp[1] = (uint8_t)c->n;
		memcpy(c->rsp + 2, c->ids, c->n * FAKE_ID_LEN);
		c->rsp_len = 2 + c->n * FAKE_ID_LEN;
		break;
	case FAKE_CMD_STORE:
		if (len != 1 + FAKE_ID_LEN || c->n >= FAKE_MAX_TEMPLATES ||
		    find(c, buf + 1) >= 0)
			c->rsp[0] = 1;
		else if (!c->lie_store)
			memcpy(c->ids[c->n++], buf + 1, FAKE_ID_LEN);
		break;
	case FAKE_CMD_DELETE:
		i = len == 1 + FAKE_ID_LEN ? find(c, buf + 1) : -1;
		if (i < 0) {
			c->rsp[0] = 1;
		} else if (!c->lie_delete) {
			remove_at(c, (size_t)i);
			if (c->delete_two && c->n > 0)
				remove_at(c, 0);
		}
		break;
	case FAKE_CMD_RESET:
		c->resets++;
		/* fall through */
	case FAKE_CMD_ARM_IDENTIFY:
	case FAKE_CMD_ENROLL_BEGIN:
		break;
	default:
		c->rsp[0] = 2; /* unknown command */
	}
	return FPAL_OK;
}

static int c_bulk_in(void *ctx, uint8_t ep, uint8_t *buf, size_t cap, size_t *got,
		     uint32_t timeout_ms) {
	struct fake_chip *c = ctx;

	(void)timeout_ms;
	if (ep != FAKE_EP_IN || !c->rsp_ready)
		return FPAL_E_TIMEOUT;
	if (c->rsp_len > cap)
		return FPAL_E_OVERFLOW;
	memcpy(buf, c->rsp, c->rsp_len);
	*got = c->rsp_len;
	c->rsp_ready = false;
	return FPAL_OK;
}

static int c_wait_bulk_in(void *ctx, uint8_t ep, uint8_t *buf, size_t cap,
			  size_t *got, uint32_t timeout_ms) {
	struct fake_chip *c = ctx;
	size_t len;

	(void)timeout_ms;
	c->waits++;
	if (ep != FAKE_EP_EVENT)
		return FPAL_E_IO;
	if (c->cancelled) {
		c->cancelled = false;
		return FPAL_E_CANCELLED;
	}
	if (c->i_events >= c->n_events)
		return FPAL_E_TIMEOUT;
	len = c->event_len[c->i_events];
	if (len > cap)
		return FPAL_E_OVERFLOW;
	memcpy(buf, c->events[c->i_events++], len);
	*got = len;
	return FPAL_OK;
}

static int c_wait_interrupt(void *ctx, uint8_t ep, uint8_t *buf, size_t cap,
			    size_t *got, uint32_t timeout_ms) {
	(void)ctx, (void)ep, (void)buf, (void)cap, (void)got, (void)timeout_ms;
	return FPAL_E_NOTSUP; /* this sensor has no interrupt endpoint */
}

static void c_cancel(void *ctx) {
	((struct fake_chip *)ctx)->cancelled = true;
}

static void c_close(void *ctx) {
	(void)ctx;
}

static const struct fpal_transport_ops chip_ops = {
	.reset = c_reset,
	.claim = c_claim,
	.release = c_release,
	.control_in = c_control_in,
	.control_out = c_control_out,
	.bulk_out = c_bulk_out,
	.bulk_in = c_bulk_in,
	.wait_interrupt = c_wait_interrupt,
	.wait_bulk_in = c_wait_bulk_in,
	.cancel = c_cancel,
	.close = c_close,
};

void fake_chip_init(struct fake_chip *c) {
	memset(c, 0, sizeof(*c));
}

struct fpal_transport fake_chip_transport(struct fake_chip *c) {
	return (struct fpal_transport){ .ops = &chip_ops, .ctx = c };
}

void fake_chip_id(uint8_t seed, struct fpal_template_id *out) {
	uint8_t raw[FAKE_ID_LEN];

	memset(raw, seed, sizeof(raw));
	(void)fpal_id_set(out, raw, sizeof(raw));
}

void fake_chip_add(struct fake_chip *c, uint8_t seed) {
	if (c->n < FAKE_MAX_TEMPLATES)
		memset(c->ids[c->n++], seed, FAKE_ID_LEN);
}

static void push_event(struct fake_chip *c, uint8_t type, const uint8_t *id) {
	size_t i = c->n_events;

	if (i >= FAKE_EVENTS_MAX)
		return;
	c->events[i][0] = type;
	c->event_len[i] = 1;
	if (id) {
		memcpy(c->events[i] + 1, id, FAKE_ID_LEN);
		c->event_len[i] += FAKE_ID_LEN;
	}
	c->n_events++;
}

void fake_chip_touch_stored(struct fake_chip *c, size_t index) {
	if (index < c->n)
		push_event(c, FAKE_EVT_MATCH, c->ids[index]);
}

void fake_chip_touch_unknown(struct fake_chip *c) {
	push_event(c, FAKE_EVT_NOMATCH, NULL);
}

void fake_chip_script_enroll(struct fake_chip *c, bool duplicate_finger) {
	if (duplicate_finger) {
		push_event(c, FAKE_EVT_DUPLICATE, NULL);
		return;
	}
	for (unsigned i = 0; i < FAKE_ENROLL_STAGES; i++)
		push_event(c, FAKE_EVT_STAGE, NULL);
}

int fake_chip_open(struct fake_chip *c, struct fpal_device **dev) {
	return fpal_device_open_transport(&fpal_driver_fake_moc, &fake_moc_usb_id,
					  fake_chip_transport(c), -1, dev);
}
