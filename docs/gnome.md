# GNOME login and lock screen

`sudo` and `su` work through `pam_fpal.so` alone. GNOME's login and lock screens need one more piece: before they offer a fingerprint, GNOME Shell asks the D-Bus service `net.reactivated.Fprint` (normally `fprintd`) whether a reader exists. With `fprintd` masked nobody answers, so GNOME never shows the option.

`fpald` is that answer. It is a small service that tells GNOME "there is one press-type reader" and nothing else.

## What GNOME actually asks

Read from GNOME Shell 50's own source (`js/gdm/util.js`):

1. `Manager.GetDefaultDevice()` on `/net/reactivated/Fprint/Manager`.
2. The `scan-type` property of the device it returns (`press` or `swipe`).

`NoSuchDevice` or a missing service means "no reader": the shell stays quiet. If a reader is reported, the shell starts the PAM service `gdm-fingerprint`, which includes `fingerprint-auth`, and the `custom/fpal` authselect profile already points that at `pam_fpal.so`. So the verification never goes through D-Bus and `fpald` never opens the sensor.

| Method / property | `fpald` answers |
|---|---|
| `Manager.GetDefaultDevice`, `GetDevices` | the device, or `NoSuchDevice` when no sensor is plugged in |
| `Device` properties `name`, `scan-type`, `num-enroll-stages` | `FPAL`, `press`, `-1` |
| `Properties`, `Introspectable`, `Peer.Ping` | standard answers |
| `Claim`, `Verify*`, `Enroll*`, `DeleteEnrolledFingers*` | `NotSupported` |
| `ListEnrolledFingers` | `NoEnrolledPrints` |

The delete methods are refused on purpose: deleting is done with `fpal delete`, in a build made for it.

## Installing

With the RPM everything is in place; the helper script starts the service and switches the PAM profile:

```sh
sudo /usr/share/fpal/authselect-fpal.sh install
```

From a checkout, install just the service first:

```sh
make
sudo make install-fpald            # /usr/local/libexec/fpald, systemd unit, bus policy
sudo scripts/authselect-fpal.sh install
```

The script reloads the bus (so it reads the policy file), enables `fpald.service` and masks `fprintd` so the two never fight over the bus name. Only one of them can own `net.reactivated.Fprint`; if `fprintd` is running, `fpald` exits with status 3 and is not restarted.

Check it:

```sh
systemctl status fpald
busctl introspect net.reactivated.Fprint /net/reactivated/Fprint/Device/0
busctl get-property net.reactivated.Fprint /net/reactivated/Fprint/Device/0 \
    net.reactivated.Fprint.Device scan-type          # s "press"
```

Then lock the screen (Super+L). The password box should show a fingerprint hint; touch the sensor to unlock. The password keeps working.

To undo it: `sudo scripts/authselect-fpal.sh revert` (stops `fpald`, unmasks `fprintd`).

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| No fingerprint hint on the lock screen | `systemctl status fpald`; `journalctl -u fpald`. Log out once so the shell asks again. |
| `fpald` fails with "cannot own net.reactivated.Fprint" | `fprintd` is running: `sudo systemctl mask --now fprintd` |
| `fpald` starts but `busctl` says access denied | The bus has not read the policy: `sudo systemctl reload dbus` |
| Works as root only / no AVC in logs expected | `sudo ausearch -m avc -ts recent` shows SELinux denials |
| Hint appears but the finger is not accepted | The PAM side: see [pam.md](pam.md), try `sudo -k; sudo true` |

## Limits

- GNOME's "Fingerprint Login" page in Settings cannot enrol: it needs `Claim` and `EnrollStart`. Enrol in Windows Hello and run `fpal bind`.
- A fingerprint login does not unlock GNOME Keyring, as with `fprintd`.
