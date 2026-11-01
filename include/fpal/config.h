/* SPDX-License-Identifier: Apache-2.0 */
/*
 * fpal global configuration.
 *
 * Every project-wide tunable lives here. Each value is guarded by #ifndef so
 * it can be overridden at build time, e.g.:
 *
 *   make CFLAGS_EXTRA='-DFPAL_FINGER_WAIT_TIMEOUT_MS=30000'
 *
 * Driver specific constants (opcodes, endpoints, magics) do NOT belong here;
 * they live in the driver's own header.
 */
#ifndef FPAL_CONFIG_H
#define FPAL_CONFIG_H

/* Release version, shown by "fpal version". */
#ifndef FPAL_VERSION
#define FPAL_VERSION "0.2.0"
#endif

/* ---- Timeouts (milliseconds) ------------------------------------------- */

/* Single command/response round trip on the bulk endpoints. */
#ifndef FPAL_CMD_TIMEOUT_MS
#define FPAL_CMD_TIMEOUT_MS 5000u
#endif

/* Control transfers during device initialisation. */
#ifndef FPAL_CONTROL_TIMEOUT_MS
#define FPAL_CONTROL_TIMEOUT_MS 5000u
#endif

/* Default time to wait for a finger to touch the sensor. */
#ifndef FPAL_FINGER_WAIT_TIMEOUT_MS
#define FPAL_FINGER_WAIT_TIMEOUT_MS 60000u
#endif

/* Default finger wait used by the PAM module. */
#ifndef FPAL_PAM_TIMEOUT_MS
#define FPAL_PAM_TIMEOUT_MS 10000u
#endif

/* Default number of identify attempts made by the PAM module. */
#ifndef FPAL_PAM_RETRIES
#define FPAL_PAM_RETRIES 3u
#endif

/*
 * Prompt shown by the PAM module before each finger wait, styled after
 * sudo's "[sudo] password for user:". Arguments: PAM service, user name.
 */
#ifndef FPAL_PAM_PROMPT_FMT
#define FPAL_PAM_PROMPT_FMT "[%s] fingerprint for %s:"
#endif

/* Upper bound accepted for the PAM "retries=" option. */
#ifndef FPAL_PAM_RETRIES_MAX
#define FPAL_PAM_RETRIES_MAX 10u
#endif

/* ---- Filesystem paths -------------------------------------------------- */

#ifndef FPAL_SYSFS_USB_DEVICES
#define FPAL_SYSFS_USB_DEVICES "/sys/bus/usb/devices"
#endif

#ifndef FPAL_DEVFS_USB_FMT
#define FPAL_DEVFS_USB_FMT "/dev/bus/usb/%03u/%03u"
#endif

/* Host side user <-> template bindings (one file per user). */
#ifndef FPAL_STATE_DIR
#define FPAL_STATE_DIR "/var/lib/fpal"
#endif

/* Serialises access to the sensor across processes. */
#ifndef FPAL_LOCK_PATH
#define FPAL_LOCK_PATH "/run/fpal.lock"
#endif

#ifndef FPAL_DBUS_SYSTEM_SOCKET
#define FPAL_DBUS_SYSTEM_SOCKET "/run/dbus/system_bus_socket"
#endif

/* Largest D-Bus message we build or accept (the spec allows 128 MiB). */
#ifndef FPAL_DBUS_MSG_MAX
#define FPAL_DBUS_MSG_MAX 16384u
#endif

/* How long to wait for the bus to answer a call (ms). */
#ifndef FPAL_DBUS_TIMEOUT_MS
#define FPAL_DBUS_TIMEOUT_MS 5000u
#endif

/* fpald exits after this long without any D-Bus traffic (ms). 0 = never,
 * which is what the systemd service wants; "fpald -t ms" overrides it. */
#ifndef FPAL_FPALD_IDLE_MS
#define FPAL_FPALD_IDLE_MS 0u
#endif

/* Device name reported to D-Bus clients (the "name" property). */
#ifndef FPAL_FPRINT_DEVICE_NAME
#define FPAL_FPRINT_DEVICE_NAME "FPAL"
#endif

/* ---- Limits ------------------------------------------------------------ */

/* Maximum number of templates any supported sensor can store. */
#ifndef FPAL_MAX_TEMPLATES
#define FPAL_MAX_TEMPLATES 10u
#endif

/* Size of a template identifier as reported by the sensor. */
#ifndef FPAL_TEMPLATE_ID_LEN
#define FPAL_TEMPLATE_ID_LEN 32u
#endif

/* Largest single bulk IN response we accept. */
#ifndef FPAL_USB_RX_MAX
#define FPAL_USB_RX_MAX 4096u
#endif

/* Largest single bulk OUT command we build. */
#ifndef FPAL_USB_TX_MAX
#define FPAL_USB_TX_MAX 1024u
#endif

/* Largest interrupt IN packet. */
#ifndef FPAL_USB_IRQ_MAX
#define FPAL_USB_IRQ_MAX 64u
#endif

/* Short strings: serial numbers, firmware versions, product names. */
#ifndef FPAL_STR_MAX
#define FPAL_STR_MAX 64u
#endif

/* ---- Environment variables -------------------------------------------- */

/* FPAL_TRACE=1 dumps every USB packet as hex to stderr. */
#define FPAL_ENV_TRACE "FPAL_TRACE"

/* FPAL_DEBUG=1 enables debug log messages. */
#define FPAL_ENV_DEBUG "FPAL_DEBUG"

#endif /* FPAL_CONFIG_H */
