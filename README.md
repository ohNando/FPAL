<div align="center">

# FPAL

**Fingerprint Auth for Linux**

Use your laptop's fingerprint sensor for `sudo`, `su` and login on Linux.
A from-scratch, dependency-free, small and readable C project.

![License](https://img.shields.io/badge/license-Apache--2.0-blue)
![Language](https://img.shields.io/badge/language-C17-informational)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey)
![Runtime](https://img.shields.io/badge/runtime-libc%20%2B%20libpam-success)
[![CI](https://github.com/ohNando/FPAL/actions/workflows/ci.yml/badge.svg)](https://github.com/ohNando/FPAL/actions/workflows/ci.yml)

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

- **Written from scratch.** The sensor's USB protocol was captured and verified on real hardware, and every step is [documented](docs/protocol-egis-moc.md). The code base is about 7,000 lines of C that you can read end to end.
- **No dependencies.** No `libusb`, no `libfprint`, no D-Bus library (the small D-Bus client is part of the project), no build system. At runtime it needs only libc and `libpam`. Building takes nothing but `make`.
- **Leaves Windows enrolments alone.** Fingers you enrolled in Windows Hello work on Linux as they are, so a dual-boot machine keeps both. Enrolling and deleting a fingerprint are guarded: they need `--allow-chip-write` and a typed confirmation, deleting works on one fingerprint at a time, and it refuses fingers FPAL did not create unless you insist. The code that runs at login (the PAM module and `fpald`) cannot write to the sensor at all, and `make ENROLL=0` builds a fully read-only `fpal`.
- **Safe defaults.** The password is always the fallback, only root can open the sensor, corrupt responses are rejected, and binding files go through symlink and permission checks.
- **Ready for more sensors.** Drivers sit behind a small interface (`struct fpal_driver`). The safety rules (look before writing, never go past the sensor's capacity, read the sensor's own list back after every write) live in the core, so a new driver cannot forget them. Supporting a new sensor means adding a folder; see [docs/adding-a-sensor.md](docs/adding-a-sensor.md), and `fpal report` describes an unknown sensor for a request.

> [!NOTE]
> Fingerprint login on Linux is already possible with [libfprint](https://gitlab.freedesktop.org/libfprint/libfprint) and `fprintd`, the mature and widely used solution. FPAL takes a different road: small, dependency-free and understandable from start to finish. The protocol knowledge was derived with libfprint's `egismoc` driver as a reference; the code itself is written from scratch.

## Features

| | |
|---|---|
| 🔐 **sudo, su, login** | `pam_fpal.so` gives fingerprint support to every program that uses PAM |
| 🖥 **GNOME login and lock screen** | `fpald` answers the question GNOME asks `fprintd`, so the fingerprint option appears |
| 🖐 **Per-user binding** | Each Linux user gets their own fingers (`fpal bind`), and you can name them (`fpal label 0 "Windows finger"`) |
| 🔍 **Transparent** | `FPAL_TRACE=1` prints every USB packet exchanged with the sensor as hex |
| 🧪 **Testable** | 200+ unit tests run against a mock USB device, a simulated sensor and a fake D-Bus, no hardware needed; the parsers are fuzzed and CI runs everything under sanitizers and in containers of four distributions |
| 🧩 **Extensible** | New sensor = new driver folder, the core stays the same; `fpal report` describes a sensor for a bug report |
| 🐧 **Several distributions** | `fpal setup` uses `authselect` on Fedora and RHEL, `pam-auth-update` on Debian and Ubuntu, and prints the steps elsewhere |
| 🪶 **Lightweight** | The only service, `fpald`, uses about 400 KB and never touches the sensor; it is opened only while you log in |

## Quick start

```sh
# Fedora: download the RPM from the latest release, then
sudo dnf install ./fpal-*.x86_64.rpm
# Debian, Ubuntu: the .deb from the release (build it with packaging/debian, see docs/usage.md)
# Arch: packaging/arch/PKGBUILD
# anything else: install gcc, make and the PAM headers, then  make && sudo make install

sudo fpal info                                    # see the sensor
sudo fpal bind $USER                              # touch the sensor, bind that finger to yourself
sudo fpal setup                                   # sudo, su and the GNOME screens now ask for a fingerprint
fpal status                                       # is everything in place? tells you what to do if not
```

Then `man fpal`, or: `sudo fpal manage` for a screen where you pick, rename, delete and add fingers with the arrow keys.

All commands, PAM setup and troubleshooting: [docs/usage.md](docs/usage.md), [docs/pam.md](docs/pam.md).

FPAL reads the fingers you enrolled in Windows Hello from the chip and binds them to a Linux user. Enrolling new fingers from Linux works too (`fpal enroll`), and so does deleting one; see [docs/usage.md](docs/usage.md).

## Status

| | |
|---|---|
| ✅ | Detect the sensor, list templates, identify a finger, bind it to a user |
| ✅ | PAM login for `sudo` and `su` |
| ✅ | GNOME login and lock screen (`fpald`, which stands in for fprintd on D-Bus) |
| ✅ | Enrolling new fingers and deleting single ones from Linux (`fpal enroll`, `fpal delete`) |
| ✅ | Naming fingers, and `fpal manage`: pick, rename, delete and add fingers with the arrow keys |
| ✅ | `fpal setup` for Fedora / RHEL (authselect) and Debian / Ubuntu (pam-auth-update); other systems get instructions |
| ✅ | `fpal report`: describe an unknown USB sensor for a bug report; a documented way to add a driver |
| 🔜 | More sensors |

**Tested hardware:** EgisTec / LighTuning `1c7a:0587` (ASUS Zenbook 14 UX3402VA), firmware `9050.6.4.67`, Fedora 44, GNOME / Wayland.
The driver also recognises the other members of the family (`0582`–`0588`, `05a1`), but those are not verified on real hardware. If you own another sensor, trying it and sharing the result would help a lot (`fpal report --markdown` and the "New sensor report" issue form).

**Distributions:** Fedora is the one tested on a real machine. The Debian, Ubuntu and Arch packages and `fpal setup` on Debian and Ubuntu are built and checked in containers by CI (the PAM files end up right and `--revert` restores them), but have not been tried on a real machine with a sensor, and the GDM fingerprint screen there is untested.

## Security

See [SECURITY.md](SECURITY.md) for the threat model and how to report a problem.

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
| [docs/architecture.md](docs/architecture.md) | How the pieces fit and where the safety rules are enforced |
| [docs/adding-a-sensor.md](docs/adding-a-sensor.md) | How to write a driver for another sensor |

## License

[Apache-2.0](LICENSE). The protocol was learned by reading libfprint's `egismoc` driver and verifying it on real hardware; no code was copied from libfprint. The one thing taken from it is data: `tools/known_sensors.h` is a table of USB vendor and product ids generated from the id tables of libfprint's drivers (`scripts/gen-known-sensors.sh`), used by `fpal report`.
