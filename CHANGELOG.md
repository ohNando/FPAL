# Changelog

All notable changes to FPAL. The format follows [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

## [0.7.0] - 2026-11-05

### Added
- A general driver interface for more sensors. Template ids are variable length (1 to 128 bytes, at least 16 for new ones) behind `fpal/id.h`; each driver states its own capacity and the length of the ids it creates; the transport can send control-out transfers and wait for the finger on a bulk IN endpoint (cancellable); sensors are described by `fpal_sensor_info` and opened with `fpal_device_open`. The binding and label files accept ids of any even hex length, so 64-character lines written by older versions keep working.
- The safety rules for writing now live in the core and apply to every driver: the sensor is listed first, a full sensor or an id that already exists is refused before the sensor is touched, and after an enrol or delete the sensor's own list is read back and must show exactly the expected change.
- `fpal report [--markdown] [--all]`: describes the USB fingerprint sensor (id, interfaces, endpoints, what libfprint calls the chip, whether FPAL supports it) for a bug report. It reads `/sys` only, needs no root and leaves out the serial number. A "New sensor report" issue form goes with it.
- `fpal setup` works on Debian and Ubuntu (`pam-auth-update`, with a PAM profile called `fpal`) as well as on Fedora and RHEL (`authselect`), and prints the steps on other systems. New options: `--dry-run`, `--backend`, `--no-services`. The helper script is now `platform.sh`; `authselect-fpal.sh` stays for one release. `--revert` also takes out the `with-fingerprint` feature when `fpal setup` was the one that added it.
- Debian and Arch packages build in CI containers; the CI also builds and tests on Fedora, Debian, Ubuntu and Arch, runs `fpal setup` against the real `authselect` and `pam-auth-update`, and checks the shell scripts and the version numbers.
- `docs/adding-a-sensor.md` and `docs/architecture.md`; a made-up sensor (`tests/drivers/fake_moc.c`) with conformance tests that every driver has to pass.

### Changed
- The read-only guarantee of the login code is checked on the symbols of the library (`nm`) instead of byte patterns alone, with each driver listing its write-only symbols in `readonly.symbols`.
- `FPAL_MAX_TEMPLATES` (now 32) only sizes arrays; the real capacity comes from the driver.

### Fixed
- The row number buffer of the `fpal list` table was too small for the compiler to prove safe under `-O1`.

## [0.6.1] - 2026-11-05

### Security
- `fpald` no longer stops when a local user sends it an oversize or malformed D-Bus message. Before, one such call (for example a 20 KB argument) made the service exit with status 0, which systemd does not restart, so the fingerprint option vanished from the GNOME screens until the next boot. Such messages are now skipped, and the service exits non-zero when the bus connection is lost so that it is started again.
- `pam_fpal.so` ignores `FPAL_TRACE` and `FPAL_DEBUG`: inside sudo the environment belongs to the user, who could otherwise make the module print sensor traffic to their terminal.
- Names of fingerprints must be well formed UTF-8 without control, invisible or direction changing characters; strings read from a USB device are reduced to printable ASCII before they are shown.
- The lock file is opened without following symlinks. Executables are linked as PIE with full RELRO, immediate binding and a non-executable stack, and built with stack clash protection.

### Fixed
- A Ctrl-C that arrived between two sensor commands was forgotten and the next finger wait ran to its timeout.

### Added
- `make fuzz`: a mutation fuzzer for the D-Bus, sensor, key press and file parsers. CI also runs the test suite and the fuzzer under AddressSanitizer, UndefinedBehaviorSanitizer and the leak checker.
- `FPAL_TRACE=1` output carries a timestamp, so it shows where the time goes.
- `SECURITY.md` (threat model, how to report) and this file.

## [0.6.0] - 2026-11-02
### Added
- `fpal manage`: a full-screen list of the fingerprints. Pick one with the arrow keys (or `j`/`k`, Home, End), rename it (`r`), delete it (`d`), add a new one bound to you (`n`) or quit (`q`, Esc). It needs root and a terminal.
- Deleting and adding ask for a typed confirmation first: `yes`, or `DELETE` for a fingerprint fpal did not create (for example from Windows Hello). Any other word, an empty line or Esc cancels, and keys pressed in the list never delete. A deleted finger is also removed from every user's bindings and its name is forgotten.
- A read-only build (`make ENROLL=0`) does not offer `d` and `n`.
### Changed
- 138 unit tests (120 in the read-only build); CI now also catches warnings from Ubuntu's compiler.

## [0.5.0] - 2026-11-02
### Added
- `fpal setup` and `fpal status`; `fpal list` as a table; man pages for `fpal`, `pam_fpal` and `fpald`.
### Changed
- `fpal enroll` and `fpal delete` are part of the normal build (guarded at run time). `pam_fpal.so` and `fpald` still cannot write to the sensor; `make ENROLL=0` builds a read-only `fpal`.

## [0.4.0] - 2026-11-02
### Added
- Names for fingerprints (`fpal label`, `enroll --name`), kept on the computer.
- `fpal delete`: removes one fingerprint, reads the sensor's list back to check, refuses fingerprints fpal did not create unless `--allow-foreign`.

## [0.3.0] - 2026-11-01
### Added
- `fpal enroll` (20 scans, then the template is stored and bound to a user) and `--dry-run`.

## [0.2.0] - 2026-11-01
### Added
- `fpald` and a small D-Bus implementation: the GNOME login and lock screens offer the fingerprint.

## [0.1.0] - 2026-10-31
### Added
- The first release: a from-scratch driver for EgisTec / LighTuning Match-on-Chip sensors, the `fpal` command line tool, and `pam_fpal.so` for `sudo` and `su`.
