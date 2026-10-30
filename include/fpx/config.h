/* SPDX-License-Identifier: Apache-2.0 */
/*
 * fpx global configuration.
 *
 * Every project-wide tunable lives here. Each value is guarded by #ifndef so
 * it can be overridden at build time, e.g.:
 *
 *   make CFLAGS_EXTRA='-DFPX_FINGER_WAIT_TIMEOUT_MS=30000'
 *
 * Driver specific constants (opcodes, endpoints, magics) do NOT belong here;
 * they live in the driver's own header.
 */
#ifndef FPX_CONFIG_H
#define FPX_CONFIG_H

/* ---- Timeouts (milliseconds) ------------------------------------------- */

/* Single command/response round trip on the bulk endpoints. */
#ifndef FPX_CMD_TIMEOUT_MS
#define FPX_CMD_TIMEOUT_MS 5000u
#endif

/* Control transfers during device initialisation. */
#ifndef FPX_CONTROL_TIMEOUT_MS
#define FPX_CONTROL_TIMEOUT_MS 5000u
#endif

/* Default time to wait for a finger to touch the sensor. */
#ifndef FPX_FINGER_WAIT_TIMEOUT_MS
#define FPX_FINGER_WAIT_TIMEOUT_MS 60000u
#endif

/* Default finger wait used by the PAM module. */
#ifndef FPX_PAM_TIMEOUT_MS
#define FPX_PAM_TIMEOUT_MS 10000u
#endif

/* Default number of identify attempts made by the PAM module. */
#ifndef FPX_PAM_RETRIES
#define FPX_PAM_RETRIES 3u
#endif

/* ---- Filesystem paths -------------------------------------------------- */

#ifndef FPX_SYSFS_USB_DEVICES
#define FPX_SYSFS_USB_DEVICES "/sys/bus/usb/devices"
#endif

#ifndef FPX_DEVFS_USB_FMT
#define FPX_DEVFS_USB_FMT "/dev/bus/usb/%03u/%03u"
#endif

/* Host side user <-> template bindings (one file per user). */
#ifndef FPX_STATE_DIR
#define FPX_STATE_DIR "/var/lib/fpx"
#endif

/* Serialises access to the sensor across processes. */
#ifndef FPX_LOCK_PATH
#define FPX_LOCK_PATH "/run/fpx.lock"
#endif

#ifndef FPX_DBUS_SYSTEM_SOCKET
#define FPX_DBUS_SYSTEM_SOCKET "/run/dbus/system_bus_socket"
#endif

/* ---- Limits ------------------------------------------------------------ */

/* Maximum number of templates any supported sensor can store. */
#ifndef FPX_MAX_TEMPLATES
#define FPX_MAX_TEMPLATES 10u
#endif

/* Size of a template identifier as reported by the sensor. */
#ifndef FPX_TEMPLATE_ID_LEN
#define FPX_TEMPLATE_ID_LEN 32u
#endif

/* Largest single bulk IN response we accept. */
#ifndef FPX_USB_RX_MAX
#define FPX_USB_RX_MAX 4096u
#endif

/* Largest single bulk OUT command we build. */
#ifndef FPX_USB_TX_MAX
#define FPX_USB_TX_MAX 1024u
#endif

/* Largest interrupt IN packet. */
#ifndef FPX_USB_IRQ_MAX
#define FPX_USB_IRQ_MAX 64u
#endif

/* Short strings: serial numbers, firmware versions, product names. */
#ifndef FPX_STR_MAX
#define FPX_STR_MAX 64u
#endif

/* ---- Environment variables -------------------------------------------- */

/* FPX_TRACE=1 dumps every USB packet as hex to stderr. */
#define FPX_ENV_TRACE "FPX_TRACE"

/* FPX_DEBUG=1 enables debug log messages. */
#define FPX_ENV_DEBUG "FPX_DEBUG"

#endif /* FPX_CONFIG_H */
