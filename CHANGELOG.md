# Changelog

All notable changes to FPAL. The format follows [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

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
- `fpal manage`: a full-screen list to pick a fingerprint with the arrow keys, rename it (`r`), delete it (`d`) or add a new one (`n`). Deleting and adding ask for a typed confirmation.

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
