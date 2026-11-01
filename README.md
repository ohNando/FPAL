<div align="center">

# FPAL

**Fingerprint Auth for Linux**

Use your laptop's fingerprint sensor for `sudo`, `su` and login on Linux.
A from-scratch, dependency-free, small and readable C project.

![License](https://img.shields.io/badge/license-Apache--2.0-blue)
![Language](https://img.shields.io/badge/language-C17-informational)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey)
![Runtime](https://img.shields.io/badge/runtime-libc%20%2B%20libpam-success)
[![CI](https://github.com/ohNando/fpal/actions/workflows/ci.yml/badge.svg)](https://github.com/ohNando/fpal/actions/workflows/ci.yml)

</div>

```console
$ sudo true
[sudo] fingerprint for ohnando:
Fingerprint not recognised, try again
[sudo] fingerprint for ohnando:
$                                  # right finger on the last try: no password asked
```

[![asciicast](https://asciinema.org/a/44R1RxA91XsLiWO2.svg)](https://asciinema.org/a/44R1RxA91XsLiWO2)

If the finger is not read, the sensor is unplugged or you are logged in over SSH, the system quietly falls back to the password. You never get locked out.

## Why FPAL?

Your laptop's fingerprint sensor works on Windows but does nothing for you on Linux. FPAL ships the driver that talks to the sensor, a command line tool and a PAM module in one small package.

- **Written from scratch.** The sensor's USB protocol was captured and verified on real hardware, and every step is [documented](docs/protocol-egis-moc.md). The code base is about 3,000 lines of C that you can read end to end.
- **No dependencies.** No `libusb`, no `libfprint`, no D-Bus library (the small D-Bus client is part of the project), no build system. At runtime it needs only libc and `libpam`. Building takes nothing but `make`.
- **Leaves Windows enrolments alone.** Fingers you enrolled in Windows Hello work on Linux as they are, so a dual-boot machine keeps both. Enrolling and deleting a fingerprint are guarded: they need `--allow-chip-write` and a typed confirmation, deleting works on one fingerprint at a time, and it refuses fingers FPAL did not create unless you insist. The code that runs at login (the PAM module and `fpald`) cannot write to the sensor at all, and `make ENROLL=0` builds a fully read-only `fpal`.
- **Safe defaults.** The password is always the fallback, only root can open the sensor, corrupt responses are rejected, and binding files go through symlink and permission checks.
- **Ready for more sensors.** Drivers sit behind a small interface (`struct fpal_driver`); supporting a new sensor means adding a folder, not touching the core.

> [!NOTE]
> Fingerprint login on Linux is already possible with [libfprint](https://gitlab.freedesktop.org/libfprint/libfprint) and `fprintd`, the mature and widely used solution. FPAL takes a different road: small, dependency-free and understandable from start to finish. The protocol knowledge was derived with libfprint's `egismoc` driver as a reference; the code itself is written from scratch.

## Features

| | |
|---|---|
| 🔐 **sudo, su, login** | `pam_fpal.so` gives fingerprint support to every program that uses PAM |
| 🖥 **GNOME login and lock screen** | `fpald` answers the question GNOME asks `fprintd`, so the fingerprint option appears |
| 🖐 **Per-user binding** | Each Linux user gets their own fingers (`fpal bind`), and you can name them (`fpal label 0 "Windows finger"`) |
| 🔍 **Transparent** | `FPAL_TRACE=1` prints every USB packet exchanged with the sensor as hex |
| 🧪 **Testable** | 115 unit tests run against a mock USB device and a fake D-Bus, no hardware needed |
| 🧩 **Extensible** | New sensor = new driver folder, the core stays the same |
| 🪶 **Lightweight** | The only service, `fpald`, uses about 400 KB and never touches the sensor; it is opened only while you log in |

## Quick start

```sh
# Fedora: download the RPM from the latest release, then
sudo dnf install ./fpal-*.x86_64.rpm
# (other systems: sudo dnf install gcc make pam-devel; make && sudo make install)

sudo fpal info                                    # see the sensor
sudo fpal bind $USER                              # touch the sensor, bind that finger to yourself
sudo fpal setup                                   # sudo, su and the GNOME screens now ask for a fingerprint
fpal status                                       # is everything in place? tells you what to do if not
```

Then `man fpal`, or: `sudo fpal list`, `sudo fpal label 0 "Windows finger"`, `sudo fpal enroll --dry-run $USER`.

All commands, PAM setup and troubleshooting: [docs/usage.md](docs/usage.md), [docs/pam.md](docs/pam.md).

FPAL reads the fingers you enrolled in Windows Hello from the chip and binds them to a Linux user. Enrolling new fingers from Linux works too (`fpal enroll`), and so does deleting one; see [docs/usage.md](docs/usage.md).

## Status

| | |
|---|---|
| ✅ | Detect the sensor, list templates, identify a finger, bind it to a user |
| ✅ | PAM login for `sudo` and `su` |
| ✅ | GNOME login and lock screen (`fpald`, which stands in for fprintd on D-Bus) |
| ✅ | Enrolling new fingers and deleting single ones from Linux (`fpal enroll`, `fpal delete`) |
| ✅ | Naming fingers |
| 🔜 | More sensors, plus packages for Debian and Arch |

**Tested hardware:** EgisTec / LighTuning `1c7a:0587` (ASUS Zenbook 14 UX3402VA), firmware `9050.6.4.67`, Fedora 44, GNOME / Wayland.
The driver also recognises the other members of the family (`0582`–`0588`, `05a1`), but those are not verified on real hardware. If you own another sensor, trying it and sharing the result would help a lot.

## Security

- Use the fingerprint to **complement the password, not replace it** (`sufficient` in PAM). With a Match-on-Chip sensor, trust rests on the "matched" answer arriving over USB, and someone with physical access to the sensor's USB lines could forge it. Do not use it for disk encryption (LUKS).
- Logging in with a fingerprint means no password is typed, so GNOME Keyring stays locked (the same limitation as with fprintd).
- Only root can open the sensor. A world-accessible udev rule was left out on purpose.

## Documentation

| | |
|---|---|
| [docs/usage.md](docs/usage.md) | Installation, commands, troubleshooting |
| [docs/pam.md](docs/pam.md) | PAM options, messages, rolling out with authselect |
| [docs/gnome.md](docs/gnome.md) | GNOME login and lock screen (`fpald`) |
| [docs/protocol-egis-moc.md](docs/protocol-egis-moc.md) | The sensor's USB protocol, verified on real hardware |

## License

[Apache-2.0](LICENSE). The protocol was learned by reading libfprint's `egismoc` driver and verifying it on real hardware; no code was copied from libfprint.
