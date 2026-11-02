/* SPDX-License-Identifier: Apache-2.0 */
/*
 * The part of net.reactivated.Fprint that GNOME needs in order to offer
 * fingerprint login: a Manager that names one device, and that device's
 * read-only properties. Verification itself does not go through D-Bus: the
 * shell starts the gdm-fingerprint PAM service, which runs pam_fpal.
 *
 * Every method that would claim, verify, enrol or delete is refused.
 */
#ifndef FPAL_DAEMON_FPRINT_SERVICE_H
#define FPAL_DAEMON_FPRINT_SERVICE_H

#include <stdbool.h>

#include "dbus/conn.h"

#define FPAL_FPRINT_BUS_NAME "net.reactivated.Fprint"
#define FPAL_FPRINT_MANAGER_PATH "/net/reactivated/Fprint/Manager"
#define FPAL_FPRINT_DEVICE_PATH "/net/reactivated/Fprint/Device/0"
#define FPAL_FPRINT_MANAGER_IFACE "net.reactivated.Fprint.Manager"
#define FPAL_FPRINT_DEVICE_IFACE "net.reactivated.Fprint.Device"
/* "press": touch sensors; fprintd's other value is "swipe". */
#define FPAL_FPRINT_SCAN_TYPE "press"

struct fpal_fprint_ops {
	/* Is a supported sensor plugged in right now? */
	bool (*sensor_present)(void *ud);
	void *ud;
};

/*
 * Answer one method call. Always sends exactly one reply (unless the caller
 * set NO_REPLY). Returns the status of sending it.
 */
int fpal_fprint_handle(struct fpal_dbus *c, const struct fpal_dmsg *call,
		       const struct fpal_fprint_ops *ops);

/*
 * Answer every call that has arrived. Oversize or malformed messages from a
 * client are dropped and the loop goes on: one local user must not be able to
 * stop the service. Returns FPAL_OK when the connection is still good,
 * FPAL_E_BUSY if another service took the bus name, or the error that ended
 * the connection.
 */
int fpal_fprint_pump(struct fpal_dbus *c, const struct fpal_fprint_ops *ops);

#endif /* FPAL_DAEMON_FPRINT_SERVICE_H */
