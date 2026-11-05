// SPDX-License-Identifier: Apache-2.0
#include "fprint_service.h"

#include <stdio.h>
#include <string.h>

#include "dbus/marshal.h"

#define IFACE_PROPS "org.freedesktop.DBus.Properties"
#define IFACE_INTRO "org.freedesktop.DBus.Introspectable"
#define IFACE_PEER "org.freedesktop.DBus.Peer"

#define ERR_UNKNOWN_OBJECT "org.freedesktop.DBus.Error.UnknownObject"
#define ERR_UNKNOWN_IFACE "org.freedesktop.DBus.Error.UnknownInterface"
#define ERR_UNKNOWN_METHOD "org.freedesktop.DBus.Error.UnknownMethod"
#define ERR_UNKNOWN_PROP "org.freedesktop.DBus.Error.UnknownProperty"
#define ERR_INVALID_ARGS "org.freedesktop.DBus.Error.InvalidArgs"
#define ERR_READ_ONLY "org.freedesktop.DBus.Error.PropertyReadOnly"
#define ERR_NOT_SUPPORTED "org.freedesktop.DBus.Error.NotSupported"
#define ERR_NO_DEVICE "net.reactivated.Fprint.Error.NoSuchDevice"
#define ERR_NO_PRINTS "net.reactivated.Fprint.Error.NoEnrolledPrints"

#define REFUSED_TEXT \
	"fpald only announces the sensor; fingerprint login goes through pam_fpal"

static const char *const refused_methods[] = {
	"Claim",	  "Release",	"VerifyStart",
	"VerifyStop",	  "EnrollStart", "EnrollStop",
	"DeleteEnrolledFingers", "DeleteEnrolledFingers2",
};

struct prop {
	const char *name;
	char type; /* 's' or 'i' */
	const char *sval;
	int32_t ival;
};

static const struct prop device_props[] = {
	{ "name", 's', FPAL_FPRINT_DEVICE_NAME, 0 },
	{ "scan-type", 's', FPAL_FPRINT_SCAN_TYPE, 0 },
	{ "num-enroll-stages", 'i', NULL, -1 }, /* enrolment is not offered */
};

#define NPROPS (sizeof(device_props) / sizeof(device_props[0]))

static const char intro_head[] =
	"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object "
	"Introspection 1.0//EN\" "
	"\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n";

static const char intro_std[] =
	"<interface name=\"" IFACE_INTRO "\">"
	"<method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/>"
	"</method></interface>"
	"<interface name=\"" IFACE_PEER "\">"
	"<method name=\"Ping\"/></interface>";

static const char intro_props[] =
	"<interface name=\"" IFACE_PROPS "\">"
	"<method name=\"Get\"><arg type=\"s\" direction=\"in\"/>"
	"<arg type=\"s\" direction=\"in\"/>"
	"<arg type=\"v\" direction=\"out\"/></method>"
	"<method name=\"GetAll\"><arg type=\"s\" direction=\"in\"/>"
	"<arg type=\"a{sv}\" direction=\"out\"/></method></interface>";

static const char intro_manager[] =
	"<interface name=\"" FPAL_FPRINT_MANAGER_IFACE "\">"
	"<method name=\"GetDevices\"><arg type=\"ao\" direction=\"out\"/>"
	"</method>"
	"<method name=\"GetDefaultDevice\">"
	"<arg type=\"o\" direction=\"out\"/></method></interface>";

#define CONST_PROP \
	"<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" " \
	"value=\"const\"/>"

static const char intro_device[] =
	"<interface name=\"" FPAL_FPRINT_DEVICE_IFACE "\">"
	"<property name=\"name\" type=\"s\" access=\"read\">" CONST_PROP
	"</property>"
	"<property name=\"scan-type\" type=\"s\" access=\"read\">" CONST_PROP
	"</property>"
	"<property name=\"num-enroll-stages\" type=\"i\" access=\"read\">"
	CONST_PROP "</property></interface>";

/* ---- helpers ----------------------------------------------------------- */

static int err(struct fpal_dbus *c, const struct fpal_dmsg *call,
	       const char *name, const char *text) {
	return fpal_dbus_reply_error(c, call, name, text);
}

static int reply_w(struct fpal_dbus *c, const struct fpal_dmsg *call,
		   const char *sig, const struct fpal_dw *w) {
	if (w->err)
		return err(c, call, "org.freedesktop.DBus.Error.Failed",
			   "reply too large");
	return fpal_dbus_reply(c, call, sig, w->p, w->len);
}

static bool iface_is(const struct fpal_dmsg *call, const char *name) {
	return call->iface && !strcmp(call->iface, name);
}

static void put_value(struct fpal_dw *w, const struct prop *p) {
	char sig[2] = { p->type, '\0' };

	fpal_dw_variant(w, sig);
	if (p->type == 's')
		fpal_dw_str(w, p->sval);
	else
		fpal_dw_i32(w, p->ival);
}

/* ---- org.freedesktop.DBus.Properties ----------------------------------- */

static int props_get(struct fpal_dbus *c, const struct fpal_dmsg *call,
		     bool is_device) {
	uint8_t buf[128];
	struct fpal_dw w;
	struct fpal_dr r;
	const char *iface, *name;

	fpal_dr_init(&r, call->body, call->body_len);
	iface = fpal_dr_str(&r);
	name = fpal_dr_str(&r);
	if (!call->sig || strcmp(call->sig, "ss") || r.err)
		return err(c, call, ERR_INVALID_ARGS, "expected (ss)");
	if (!is_device || strcmp(iface, FPAL_FPRINT_DEVICE_IFACE))
		return err(c, call, ERR_UNKNOWN_IFACE, iface);

	for (size_t i = 0; i < NPROPS; i++) {
		if (strcmp(name, device_props[i].name))
			continue;
		fpal_dw_init(&w, buf, sizeof(buf));
		put_value(&w, &device_props[i]);
		return reply_w(c, call, "v", &w);
	}
	return err(c, call, ERR_UNKNOWN_PROP, name);
}

static int props_get_all(struct fpal_dbus *c, const struct fpal_dmsg *call,
			 bool is_device) {
	uint8_t buf[256];
	struct fpal_dw w;
	struct fpal_dw_arr arr;
	struct fpal_dr r;
	const char *iface;

	fpal_dr_init(&r, call->body, call->body_len);
	iface = fpal_dr_str(&r);
	if (!call->sig || strcmp(call->sig, "s") || r.err)
		return err(c, call, ERR_INVALID_ARGS, "expected (s)");

	/* An empty interface name means "all interfaces" (the spec). */
	if (is_device && *iface && strcmp(iface, FPAL_FPRINT_DEVICE_IFACE))
		return err(c, call, ERR_UNKNOWN_IFACE, iface);

	fpal_dw_init(&w, buf, sizeof(buf));
	fpal_dw_array_begin(&w, 8, &arr);
	/* The Manager has no properties: its dictionary stays empty. */
	for (size_t i = 0; is_device && i < NPROPS; i++) {
		fpal_dw_align(&w, 8);
		fpal_dw_str(&w, device_props[i].name);
		put_value(&w, &device_props[i]);
	}
	fpal_dw_array_end(&w, &arr);
	return reply_w(c, call, "a{sv}", &w);
}

/* ---- Introspect -------------------------------------------------------- */

static int introspect(struct fpal_dbus *c, const struct fpal_dmsg *call,
		      bool is_device) {
	char xml[2048];
	uint8_t buf[2200];
	struct fpal_dw w;
	int n;

	if (is_device)
		n = snprintf(xml, sizeof(xml), "%s<node>%s%s%s</node>\n",
			     intro_head, intro_std, intro_props, intro_device);
	else
		n = snprintf(xml, sizeof(xml),
			     "%s<node>%s%s%s<node name=\"Device\"/></node>\n",
			     intro_head, intro_std, intro_props, intro_manager);
	if (n < 0 || (size_t)n >= sizeof(xml))
		return err(c, call, "org.freedesktop.DBus.Error.Failed", NULL);
	fpal_dw_init(&w, buf, sizeof(buf));
	fpal_dw_str(&w, xml);
	return reply_w(c, call, "s", &w);
}

/* ---- Manager / Device -------------------------------------------------- */

static int manager_call(struct fpal_dbus *c, const struct fpal_dmsg *call,
			bool present) {
	uint8_t buf[96];
	struct fpal_dw w;
	struct fpal_dw_arr arr;

	fpal_dw_init(&w, buf, sizeof(buf));
	if (!strcmp(call->member, "GetDevices")) {
		fpal_dw_array_begin(&w, 4, &arr);
		if (present)
			fpal_dw_str(&w, FPAL_FPRINT_DEVICE_PATH);
		fpal_dw_array_end(&w, &arr);
		return reply_w(c, call, "ao", &w);
	}
	if (!strcmp(call->member, "GetDefaultDevice")) {
		if (!present)
			return err(c, call, ERR_NO_DEVICE,
				   "no fingerprint sensor found");
		fpal_dw_str(&w, FPAL_FPRINT_DEVICE_PATH);
		return reply_w(c, call, "o", &w);
	}
	return err(c, call, ERR_UNKNOWN_METHOD, call->member);
}

static int device_call(struct fpal_dbus *c, const struct fpal_dmsg *call) {
	if (!strcmp(call->member, "ListEnrolledFingers"))
		return err(c, call, ERR_NO_PRINTS, NULL);
	for (size_t i = 0;
	     i < sizeof(refused_methods) / sizeof(refused_methods[0]); i++) {
		if (!strcmp(call->member, refused_methods[i]))
			return err(c, call, ERR_NOT_SUPPORTED, REFUSED_TEXT);
	}
	return err(c, call, ERR_UNKNOWN_METHOD, call->member);
}

int fpal_fprint_handle(struct fpal_dbus *c, const struct fpal_dmsg *call,
		       const struct fpal_fprint_ops *ops) {
	bool present = ops->sensor_present(ops->ud);
	bool is_manager = !strcmp(call->path, FPAL_FPRINT_MANAGER_PATH);
	bool is_device = !strcmp(call->path, FPAL_FPRINT_DEVICE_PATH);

	if (iface_is(call, IFACE_PEER)) {
		if (!strcmp(call->member, "Ping"))
			return fpal_dbus_reply(c, call, NULL, NULL, 0);
		return err(c, call, ERR_UNKNOWN_METHOD, call->member);
	}
	if (!is_manager && !(is_device && present))
		return err(c, call, ERR_UNKNOWN_OBJECT, call->path);

	if (iface_is(call, IFACE_INTRO)) {
		if (!strcmp(call->member, "Introspect"))
			return introspect(c, call, is_device);
		return err(c, call, ERR_UNKNOWN_METHOD, call->member);
	}
	if (iface_is(call, IFACE_PROPS)) {
		if (!strcmp(call->member, "Get"))
			return props_get(c, call, is_device);
		if (!strcmp(call->member, "GetAll"))
			return props_get_all(c, call, is_device);
		if (!strcmp(call->member, "Set"))
			return err(c, call, ERR_READ_ONLY, NULL);
		return err(c, call, ERR_UNKNOWN_METHOD, call->member);
	}
	if (is_manager && (!call->iface ||
			   iface_is(call, FPAL_FPRINT_MANAGER_IFACE)))
		return manager_call(c, call, present);
	if (is_device && (!call->iface ||
			  iface_is(call, FPAL_FPRINT_DEVICE_IFACE)))
		return device_call(c, call);
	return err(c, call, ERR_UNKNOWN_IFACE,
		   call->iface ? call->iface : "");
}

int fpal_fprint_pump(struct fpal_dbus *c, const struct fpal_fprint_ops *ops) {
	struct fpal_dmsg m;
	int rc;

	for (;;) {
		rc = fpal_dbus_recv(c, 0, &m);
		if (rc == FPAL_E_TIMEOUT)
			return FPAL_OK; /* nothing more waiting */
		if (rc == FPAL_E_DROPPED)
			continue;
		if (rc)
			return rc;
		if (m.type == FPAL_DMSG_SIGNAL && m.member &&
		    !strcmp(m.member, "NameLost"))
			return FPAL_E_BUSY;
		if (m.type == FPAL_DMSG_CALL)
			(void)fpal_fprint_handle(c, &m, ops);
	}
}
