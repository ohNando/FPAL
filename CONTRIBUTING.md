# Contributing to FPAL

Thanks for helping. This file covers the workflow, the code style and how to add support for a new sensor.

## Ground rules

- **Writing to the sensor is narrow and guarded.** Enrolling and deleting (`INS 0x16` and `INS 0x18` for Egis) need `--allow-chip-write` (or `--dry-run`) and a typed confirmation, and live only behind `#ifdef FPAL_ENABLE_ENROLL`, which `make ENROLL=0` leaves out. The code that runs at login (`pam_fpal.so`, `fpald`) links a library built without it, and `make test` checks the binaries (`scripts/check-readonly.sh`). Deleting handles exactly one template and reads the list back to confirm; a patch that deletes several templates, clears the sensor, or deletes without confirmation will not be accepted. Fingerprints FPAL did not create need `--allow-foreign`.
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

The steps, the driver interface and a checklist are in [docs/adding-a-sensor.md](docs/adding-a-sensor.md); how the pieces fit together is in [docs/architecture.md](docs/architecture.md). In short: a driver is one `struct fpal_driver` ([src/core/driver.h](src/core/driver.h)), registered in `src/core/registry.c`, tested without hardware (the made-up sensor in `tests/drivers/fake_moc.c` is the example) and documented in `docs/protocol-<name>.md`. Run `fpal report --markdown` to describe a sensor in an issue.

Only the sensor in the README's "Tested hardware" line has been verified by the maintainer; reports from other devices are very welcome.
