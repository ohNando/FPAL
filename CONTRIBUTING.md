# Contributing to FPAL

Thanks for helping. This file covers the workflow, the code style and how to add support for a new sensor.

## Ground rules

- **Writing to the sensor is opt-in and narrow.** Enrolling and deleting (`INS 0x16` and `INS 0x18` for Egis) may only exist behind `make ENROLL=1`, `--allow-chip-write` and a typed confirmation; the default build must stay unable to write (a test checks it). Deleting handles exactly one template and reads the list back to confirm; a patch that deletes several templates, clears the sensor, or deletes without confirmation will not be accepted. Fingerprints FPAL did not create need `--allow-foreign`.
- **No new runtime dependencies.** libc and `libpam` only. No libusb, no D-Bus library.
- **The password stays a fallback.** PAM integration must be `sufficient`.
- Only root opens the sensor. Do not add a world-accessible udev rule.

## Workflow

- Work on `develop`; `main` receives `--no-ff` merges at milestones and releases are tagged from it.
- Small commits with a conventional prefix and a one-line message: `feat:`, `fix:`, `docs:`, `test:`, `build:`, `chore:`, `refactor:`, `style:`.
- `make && make test` must pass (`-Werror` is on). CI runs the same on Ubuntu with gcc.

## Code style

- C17, Linux kernel style: tabs, 80 columns, **opening brace on the same line, also for functions** (`int main(void) {`).
- Fixed-width `<stdint.h>` types, the smallest that fits.
- Tunables (timeouts, paths, limits) live in `include/fpal/config.h` behind `#ifndef`; shared types in `include/fpal/types.h`.
- Everything is in English: code, comments, docs, commits.

## Adding a sensor

A driver is one `struct fpal_driver` ([src/core/driver.h](src/core/driver.h)):

1. Create `src/drivers/<name>/` and implement `open`, `close`, `list` and `identify` on top of the transport (`dev->tr`). `enroll` stays `NULL` unless built with `ENROLL=1`.
2. List the USB ids in an array terminated by `vid == 0` and add the driver to `fpal_drivers[]` in `src/core/registry.c`.
3. Document the wire protocol in `docs/protocol-<name>.md`, marking what you verified on real hardware.
4. Test it without hardware: `tests/mock_transport.c` replays scripted USB exchanges (see `tests/test_egis_moc.c`). Use made-up template ids, never ones from a real device.
5. Run `sudo FPAL_TRACE=1 fpal identify` on your sensor and attach the trace to your pull request.

Only the sensor in the README's "Tested hardware" line has been verified by the maintainer; reports from other devices are very welcome.
