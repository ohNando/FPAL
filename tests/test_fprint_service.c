// SPDX-License-Identifier: Apache-2.0
/*
 * net.reactivated.Fprint service tests: the handler is fed method calls and
 * its replies are read back from the fake bus.
 */
#include "../daemon/fprint_service.h"
#include "fake_bus.h"

static bool sensor_plugged;

static bool sensor_present(void *ud) {
	(void)ud;
	return sensor_plugged;
}

static const struct fpal_fprint_ops ops = { .sensor_present = sensor_present };

struct req {
	const char *path;
	const char *iface;
	const char *member;
	const char *sig;
	const uint8_t *body;
	uint32_t body_len;
};

/* Send @rq to the handler and parse its single reply into @reply. */
static void ask(struct fake *f, const struct req *rq, uint8_t *rx, size_t cap,
		struct fpal_dmsg *reply) {
	struct fpal_dmsg call = { .type = FPAL_DMSG_CALL, .serial = 21,
				  .sender = ":1.9", .path = rq->path,
				  .iface = rq->iface, .member = rq->member,
				  .sig = rq->sig, .body = rq->body,
				  .body_len = rq->body_len };
	size_t n;

	T_EQ_INT(fpal_fprint_handle(&f->c, &call, &ops), FPAL_OK);
	n = srv_read(f->peer, rx, cap);
	T_EQ_INT(fpal_dmsg_parse(rx, n, reply), FPAL_OK);
	T_EQ_INT(reply->reply_serial, 21);
	T_CHECK(!strcmp(reply->dest, ":1.9"));
}

static void setup(struct fake *f) {
	uint8_t drain[512];

	T_EQ_INT(fake_open(f), FPAL_OK);
	(void)srv_read(f->peer, drain, sizeof(drain));
}

static void teardown(struct fake *f) {
	fpal_dbus_close(&f->c);
	close(f->peer);
}

static bool is_error(const struct fpal_dmsg *m, const char *name) {
	return m->type == FPAL_DMSG_ERROR && !strcmp(m->errname, name);
}

static uint32_t body_ss(uint8_t *buf, size_t cap, const char *a,
			const char *b) {
	struct fpal_dw w;

	fpal_dw_init(&w, buf, cap);
	fpal_dw_str(&w, a);
	if (b)
		fpal_dw_str(&w, b);
	return (uint32_t)w.len;
}

#define MGR FPAL_FPRINT_MANAGER_PATH
#define DEV FPAL_FPRINT_DEVICE_PATH
#define MGR_IF FPAL_FPRINT_MANAGER_IFACE
#define DEV_IF FPAL_FPRINT_DEVICE_IFACE
#define PROPS "org.freedesktop.DBus.Properties"

static void test_default_device_when_plugged(void) {
	struct fake f;
	uint8_t rx[512];
	struct fpal_dmsg r;
	struct fpal_dr d;
	struct req q = { MGR, MGR_IF, "GetDefaultDevice", NULL, NULL, 0 };

	setup(&f);
	sensor_plugged = true;
	ask(&f, &q, rx, sizeof(rx), &r);
	T_EQ_INT(r.type, FPAL_DMSG_RETURN);
	T_CHECK(!strcmp(r.sig, "o"));
	fpal_dr_init(&d, r.body, r.body_len);
	T_CHECK(!strcmp(fpal_dr_str(&d), DEV));
	teardown(&f);
}

static void test_no_device_when_unplugged(void) {
	struct fake f;
	uint8_t rx[512];
	struct fpal_dmsg r;
	struct fpal_dr d;
	struct req q = { MGR, MGR_IF, "GetDefaultDevice", NULL, NULL, 0 };
	struct req g = { MGR, MGR_IF, "GetDevices", NULL, NULL, 0 };

	setup(&f);
	sensor_plugged = false;
	ask(&f, &q, rx, sizeof(rx), &r);
	/* GNOME treats exactly this error as "no reader": it stays silent */
	T_CHECK(is_error(&r, "net.reactivated.Fprint.Error.NoSuchDevice"));

	ask(&f, &g, rx, sizeof(rx), &r);
	T_CHECK(!strcmp(r.sig, "ao"));
	fpal_dr_init(&d, r.body, r.body_len);
	T_EQ_INT(fpal_dr_array_begin(&d, 4), 0);

	/* the device object is gone, too */
	{
		struct req p = { DEV, PROPS, "Get", "ss", NULL, 0 };
		uint8_t body[96];

		p.body_len = body_ss(body, sizeof(body), DEV_IF, "scan-type");
		p.body = body;
		ask(&f, &p, rx, sizeof(rx), &r);
		T_CHECK(is_error(&r, "org.freedesktop.DBus.Error.UnknownObject"));
	}
	teardown(&f);
}

static void test_get_devices(void) {
	struct fake f;
	uint8_t rx[512];
	struct fpal_dmsg r;
	struct fpal_dr d;
	struct req q = { MGR, MGR_IF, "GetDevices", NULL, NULL, 0 };

	setup(&f);
	sensor_plugged = true;
	ask(&f, &q, rx, sizeof(rx), &r);
	fpal_dr_init(&d, r.body, r.body_len);
	T_EQ_INT(fpal_dr_array_begin(&d, 4), 4 + strlen(DEV) + 1);
	T_CHECK(!strcmp(fpal_dr_str(&d), DEV));
	T_EQ_INT(d.err, 0);
	teardown(&f);
}

static void test_scan_type_property(void) {
	struct fake f;
	uint8_t rx[512], body[96];
	struct fpal_dmsg r;
	struct fpal_dr d;
	struct req q = { DEV, PROPS, "Get", "ss", body, 0 };

	setup(&f);
	sensor_plugged = true;
	q.body_len = body_ss(body, sizeof(body), DEV_IF, "scan-type");
	ask(&f, &q, rx, sizeof(rx), &r);
	T_CHECK(!strcmp(r.sig, "v"));
	fpal_dr_init(&d, r.body, r.body_len);
	T_CHECK(!strcmp(fpal_dr_sig(&d), "s"));
	T_CHECK(!strcmp(fpal_dr_str(&d), "press")); /* the value GNOME maps */
	teardown(&f);
}

static void test_property_errors(void) {
	struct fake f;
	uint8_t rx[512], body[96];
	struct fpal_dmsg r;
	struct req q = { DEV, PROPS, "Get", "ss", body, 0 };

	setup(&f);
	sensor_plugged = true;

	q.body_len = body_ss(body, sizeof(body), DEV_IF, "nope");
	ask(&f, &q, rx, sizeof(rx), &r);
	T_CHECK(is_error(&r, "org.freedesktop.DBus.Error.UnknownProperty"));

	q.body_len = body_ss(body, sizeof(body), "other.Iface", "scan-type");
	ask(&f, &q, rx, sizeof(rx), &r);
	T_CHECK(is_error(&r, "org.freedesktop.DBus.Error.UnknownInterface"));

	q.sig = "s"; /* wrong signature */
	ask(&f, &q, rx, sizeof(rx), &r);
	T_CHECK(is_error(&r, "org.freedesktop.DBus.Error.InvalidArgs"));

	q.sig = "ss";
	q.body_len = 3; /* truncated body */
	ask(&f, &q, rx, sizeof(rx), &r);
	T_CHECK(is_error(&r, "org.freedesktop.DBus.Error.InvalidArgs"));

	q.member = "Set";
	ask(&f, &q, rx, sizeof(rx), &r);
	T_CHECK(is_error(&r, "org.freedesktop.DBus.Error.PropertyReadOnly"));
	teardown(&f);
}

static void test_get_all(void) {
	struct fake f;
	uint8_t rx[512], body[96];
	struct fpal_dmsg r;
	struct fpal_dr d;
	struct req q = { DEV, PROPS, "GetAll", "s", body, 0 };
	uint32_t n, end;
	unsigned seen = 0;

	setup(&f);
	sensor_plugged = true;
	q.body_len = body_ss(body, sizeof(body), DEV_IF, NULL);
	ask(&f, &q, rx, sizeof(rx), &r);
	T_CHECK(!strcmp(r.sig, "a{sv}"));

	fpal_dr_init(&d, r.body, r.body_len);
	n = fpal_dr_array_begin(&d, 8);
	end = (uint32_t)d.off + n;
	while (d.off < end && !d.err) {
		const char *key, *vsig;

		fpal_dr_align(&d, 8);
		key = fpal_dr_str(&d);
		vsig = fpal_dr_sig(&d);
		if (d.err)
			break;
		if (!strcmp(key, "name")) {
			T_CHECK(!strcmp(vsig, "s"));
			T_CHECK(!strcmp(fpal_dr_str(&d), "FPAL"));
			seen |= 1;
		} else if (!strcmp(key, "scan-type")) {
			T_CHECK(!strcmp(fpal_dr_str(&d), "press"));
			seen |= 2;
		} else if (!strcmp(key, "num-enroll-stages")) {
			T_CHECK(!strcmp(vsig, "i"));
			T_EQ_INT((int32_t)fpal_dr_u32(&d), -1);
			seen |= 4;
		}
	}
	T_EQ_INT(d.err, 0);
	T_EQ_INT(seen, 7);
	T_EQ_INT(d.off, r.body_len);

	/* the Manager has no properties */
	{
		struct req m = { MGR, PROPS, "GetAll", "s", body, 0 };

		m.body_len = body_ss(body, sizeof(body), MGR_IF, NULL);
		ask(&f, &m, rx, sizeof(rx), &r);
		fpal_dr_init(&d, r.body, r.body_len);
		T_EQ_INT(fpal_dr_array_begin(&d, 8), 0);
	}
	teardown(&f);
}

static void test_everything_else_is_refused(void) {
	static const char *const refused[] = {
		"Claim", "Release", "VerifyStart", "VerifyStop",
		"EnrollStart", "EnrollStop", "DeleteEnrolledFingers",
		"DeleteEnrolledFingers2",
	};
	struct fake f;
	uint8_t rx[512];
	struct fpal_dmsg r;
	struct req q = { DEV, DEV_IF, NULL, NULL, NULL, 0 };

	setup(&f);
	sensor_plugged = true;
	for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
		q.member = refused[i];
		ask(&f, &q, rx, sizeof(rx), &r);
		T_CHECK(is_error(&r, "org.freedesktop.DBus.Error.NotSupported"));
	}

	q.member = "ListEnrolledFingers";
	ask(&f, &q, rx, sizeof(rx), &r);
	T_CHECK(is_error(&r, "net.reactivated.Fprint.Error.NoEnrolledPrints"));

	q.member = "Frobnicate";
	ask(&f, &q, rx, sizeof(rx), &r);
	T_CHECK(is_error(&r, "org.freedesktop.DBus.Error.UnknownMethod"));
	teardown(&f);
}

static void test_introspect_and_ping(void) {
	struct fake f;
	uint8_t rx[4096];
	struct fpal_dmsg r;
	struct fpal_dr d;
	struct req mi = { MGR, "org.freedesktop.DBus.Introspectable",
			  "Introspect", NULL, NULL, 0 };
	struct req di = { DEV, "org.freedesktop.DBus.Introspectable",
			  "Introspect", NULL, NULL, 0 };
	struct req ping = { DEV, "org.freedesktop.DBus.Peer", "Ping", NULL,
			    NULL, 0 };
	struct req bad = { "/nowhere", MGR_IF, "GetDevices", NULL, NULL, 0 };

	setup(&f);
	sensor_plugged = true;

	ask(&f, &mi, rx, sizeof(rx), &r);
	fpal_dr_init(&d, r.body, r.body_len);
	T_CHECK(strstr(fpal_dr_str(&d), MGR_IF) != NULL);

	ask(&f, &di, rx, sizeof(rx), &r);
	fpal_dr_init(&d, r.body, r.body_len);
	T_CHECK(strstr(fpal_dr_str(&d), "scan-type") != NULL);

	ask(&f, &ping, rx, sizeof(rx), &r);
	T_EQ_INT(r.type, FPAL_DMSG_RETURN);
	T_EQ_INT(r.body_len, 0);

	ask(&f, &bad, rx, sizeof(rx), &r);
	T_CHECK(is_error(&r, "org.freedesktop.DBus.Error.UnknownObject"));
	teardown(&f);
}

int main(void) {
	T_RUN(test_default_device_when_plugged);
	T_RUN(test_no_device_when_unplugged);
	T_RUN(test_get_devices);
	T_RUN(test_scan_type_property);
	T_RUN(test_property_errors);
	T_RUN(test_get_all);
	T_RUN(test_everything_else_is_refused);
	T_RUN(test_introspect_and_ping);
	return T_DONE();
}
