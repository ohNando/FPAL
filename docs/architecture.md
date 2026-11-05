# Architecture

fpal is a fingerprint stack for Match-on-Chip sensors: the sensor stores the fingers and does the matching, the computer only learns **which stored template matched**. Everything below is plain C17; the only runtime dependencies are libc and libpam.

```
 sudo / su / GDM            GNOME Shell            a person at a terminal
      │                          │                          │
 pam_fpal.so ──────────────  fpald  ──────────────────────  fpal (CLI, TUI)
      │   (read-only)        (read-only)                    │
      └───────────┬──────────────┴──────────────┬───────────┘
                  │            libfpal           │
        ┌─────────┴──────────────────────────────┴─────────┐
        │ core: device.c  registry.c  id.c  lock.c  newid.c │  safety rules
        ├────────────────────────┬─────────────────────────┤
        │ storage: bindings,     │ drivers: egis_moc, ...  │  one per sensor family
        │ labels (/var/lib/fpal) │   (struct fpal_driver)  │
        ├────────────────────────┴─────────────────────────┤
        │ transport: usbdevfs (real) / mock (tests)         │  the only code that
        └───────────────────────────────────────────────────┘  touches the kernel
```

## The layers

**Transport** ([`src/transport/`](../src/transport/)). A table of operations: reset, claim/release, control in and out, bulk in and out, two cancellable waits (interrupt and bulk IN), cancel. `usbdevfs.c` implements them with raw ioctls on `/dev/bus/usb/...`; no libusb. Tests use `tests/mock_transport.c` (a scripted conversation) or a simulated chip. A driver never calls the kernel, which is why every driver can be tested without hardware.

**Drivers** ([`src/drivers/`](../src/drivers/)). One `struct fpal_driver` per sensor family: `open/close/list/identify`, and `enroll/delete_template` as write primitives. A driver knows its wire protocol and its limits (`max_templates`, `new_id_len`), nothing about users, PAM or files. See [adding-a-sensor.md](adding-a-sensor.md).

**Core** ([`src/core/`](../src/core/)). `registry.c` finds supported sensors in sysfs and holds the driver table; `device.c` opens a device and is the only place that enrols and deletes; `id.c` is the template id type; `lock.c` takes `/run/fpal.lock` so two processes never talk to the sensor at once; `trace.c` and `log.c` give `FPAL_TRACE` and `FPAL_DEBUG`.

**Storage** ([`src/storage/`](../src/storage/)). Which finger belongs to which user (`/var/lib/fpal/<user>`, one hex id per line) and the names of fingers (`/var/lib/fpal/.labels`). Root-owned, mode 0600, no symlinks followed, replaced atomically. Lines of any even hex length from 2 to 256 characters are accepted, so ids of different sensors can live side by side; malformed or over-long lines are skipped, never parsed in pieces.

**Front ends.**
- [`pam/`](../pam/): `pam_fpal.so`, called by `sudo`, `su`, GDM. It asks the sensor which finger matched and succeeds only if that id is bound to the user. It is `sufficient`, so the password always remains.
- [`daemon/`](../daemon/): `fpald`, a tiny `net.reactivated.Fprint` service whose only job is to tell GNOME "there is a press-type reader" so the lock screen offers a fingerprint. It never opens the sensor.
- [`tools/`](../tools/): the `fpal` command line, the `manage` screen, `status`, `setup`, `report`.
- [`src/dbus/`](../src/dbus/): the small D-Bus client and marshaller `fpald` and `status` use. No D-Bus library.

## Safety rules, and where they are enforced

| Rule | Enforced by |
|---|---|
| Nothing is written to the chip unless `--allow-chip-write` (or `--dry-run`) and a typed confirmation | `tools/fpal.c`, `tools/manage.c` |
| Delete removes exactly **one** template; there is no "clear all" anywhere | `fpal_delete()`; drivers expose no such operation |
| Look before writing: list first, refuse when full (`FPAL_E_FULL`), when the id exists (`FPAL_E_EXISTS`), or when deleting an unknown id (`FPAL_E_NODATA`) | `fpal_enroll()` / `fpal_delete()` in `src/core/device.c`, for every driver |
| After a write, read the chip's own list back: the new id is there / exactly the deleted id is gone | the same two functions |
| Login code cannot write to the chip | `pam_fpal.so` and `fpald` link `libfpal-ro.a`, built **without** `FPAL_ENABLE_ENROLL`; `make test` runs `scripts/check-readonly.sh` (symbols listed per driver in `readonly.symbols`, via `nm`) on the library and the binaries |
| `make ENROLL=0` builds a `fpal` that cannot write either | the same define; the check runs on the full library too |
| The password stays the fallback | PAM line is `sufficient` |
| Sensor access is root-only | no udev rule is installed |
| Ids are long and unique | `fpal/id.h` is the only way to build or compare one; drivers declare `new_id_len` (16 to 128 bytes) |

Writing is compiled out where it is not needed. In a read-only build `fpal_enroll()` and `fpal_delete()` are stubs that return `FPAL_E_NOTSUP`, and the drivers' write operations are `NULL`.

## What happens when you touch the sensor

`sudo true` with a bound finger:

1. `pam_fpal.so` opens the first supported sensor (`fpal_device_open_first`: scan sysfs, pick the driver by `vid:pid`, take the lock, open usbdevfs, claim the interface, run the driver's `open`).
2. It calls `fpal_identify`. The driver waits for the finger (interrupt or bulk wait, cancellable) and returns the id of the matching template, after checking that id is one the chip really holds.
3. The module loads the user's bindings and compares ids with `fpal_id_equal`. A match means success; anything else (no match, timeout, no sensor, error) means "ignore", and PAM moves on to the password.

## Files

| Path | Purpose |
|---|---|
| `include/fpal/` | public headers: `config.h` (all tunables), `types.h`, `id.h`, `fpal.h`, `bindings.h`, `labels.h` |
| `src/core/driver.h` | the driver interface |
| `src/transport/transport.h` | the transport interface |
| `src/drivers/<name>/` | one driver, plus `readonly.symbols` and `readonly.patterns` |
| `tests/` | one `test_*.c` per area; `drivers/fake_moc.*` is a made-up sensor; `golden/` are recorded frame transcripts of the Egis driver that must never change by accident |
| `scripts/` | `check-readonly.sh`, `check-version.sh`, `ci-container.sh`, `capture-trace.sh`, `gen-known-sensors.sh`, the PAM setup script |
| `docs/` | this page, [adding-a-sensor.md](adding-a-sensor.md), [protocol-egis-moc.md](protocol-egis-moc.md), [gnome.md](gnome.md), [pam.md](pam.md), [usage.md](usage.md) |
