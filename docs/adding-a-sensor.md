# Adding a sensor

fpal supports **Match-on-Chip** sensors over USB: the fingerprints are stored and matched on the sensor itself and the computer only learns the id of the template that matched. Image sensors (the computer does the matching) are out of scope.

This is the whole job, in order. The worked example is the made-up sensor in [`tests/drivers/fake_moc.c`](../tests/drivers/fake_moc.c): it is written against the same API, runs in every `make test` and cannot go out of date. Read it next to this page. The real driver, [`src/drivers/egis_moc/`](../src/drivers/egis_moc/), shows the same shape with a real protocol.

## 0. Find out what the sensor is

```sh
fpal report --markdown
```

No root, nothing is sent to the device, the serial number is left out. It prints the USB id, the interfaces and endpoints, and what libfprint calls the chip. If libfprint has a driver for it, that source is the best protocol reference there is (`dnf download --source libfprint`); fpal's drivers are written from scratch, not copied.

If you are the owner of a sensor fpal does not know, open an issue with the "New sensor report" form and paste that output. A driver can only be tested on the real hardware, so say whether you can run commands as root for a trace.

## 1. What a driver is

One `struct fpal_driver` ([`src/core/driver.h`](../src/core/driver.h)):

| Field | What it is |
|---|---|
| `name` | short unique name, `"egis_moc"` |
| `ids` | USB ids, `{ vid, pid, quirk_flags }`, ended by `vid == 0` |
| `caps` | `FPAL_CAP_MOC`, plus `FPAL_CAP_ENROLL` when it can write |
| `priv_size` | size of your private state, allocated and zeroed for you (`dev->priv`) |
| `max_templates` | how many fingers the chip holds, 1 to `FPAL_MAX_TEMPLATES` |
| `new_id_len` | length of the ids fpal makes for new fingers, 16 to `FPAL_ID_MAX` |
| `open` / `close` | claim the interface, bring the chip up, read the firmware string into `dev->fw_version`; undo it |
| `list` | the ids stored on the chip |
| `identify` | wait for a finger, return the id that matched |
| `enroll`, `delete_template` | the write primitives, **only under `#ifdef FPAL_ENABLE_ENROLL`**, otherwise `NULL` |

Your code talks to the hardware only through `dev->tr`, the transport ([`src/transport/transport.h`](../src/transport/transport.h)): `claim`/`release`, `reset`, `control_in`, `control_out`, `bulk_out`, `bulk_in`, and two waits that `fpal_cancel()` can abort, `wait_interrupt` and `wait_bulk_in`. Use whichever your chip signals the finger with. Never call the kernel yourself; that is what keeps the driver testable.

## 2. Template ids

An id is `struct fpal_template_id { uint8_t len; uint8_t data[FPAL_ID_MAX]; }`. **Never touch `len` or `data` directly**; use the helpers in [`fpal/id.h`](../include/fpal/id.h) (`fpal_id_set`, `fpal_id_equal`, `fpal_id_data`, `fpal_id_len`, ...). Two rules:

- **At least 16 bytes, unique to the chip, mostly random.** The ids end up in the binding files that decide who may log in. A chip whose own ids are slot numbers (`0`, `1`, ...) must wrap them in something longer: a slot is reused after a delete, and a stale binding would then match a different finger.
- **The length must be the same as `new_id_len`** for the ids you create. Ids you only read can be any length from 1 to `FPAL_ID_MAX`.

## 3. What the core does for you (and what you must not do)

`fpal_enroll()` and `fpal_delete()` in [`src/core/device.c`](../src/core/device.c) own the safety rules, so your driver cannot forget them:

- they **list the chip first** and refuse before touching it: `FPAL_E_FULL` when it is at `max_templates`, `FPAL_E_EXISTS` when the id is already there, `FPAL_E_NODATA` when you ask to delete an id that is not;
- after an enroll they **read the list back** and require the new id; after a delete they require that exactly that id is gone and every other one still there. The chip's own list is trusted, never a status word.

So `enroll` only runs the chip's enrol sequence: it receives the ids already stored, leaves the chip idle whatever happens, returns `FPAL_E_EXISTS` if the chip says the finger is already known, and stores nothing when `FPAL_ENROLL_DRY_RUN` is set. `delete_template` only sends the command that deletes **one** template. There is no clear-all anywhere, and a driver must not offer one.

`identify` must never report an id the chip does not hold. The fake driver checks its match against its own list; the Egis one does the same.

## 4. Steps

1. **Create `src/drivers/<name>/`** with the driver (and a `proto.c` + header if the framing is more than a few lines, so it can be tested byte for byte).
2. **Register it:** `extern const struct fpal_driver fpal_driver_<name>;` and one line in `fpal_drivers[]` in [`src/core/registry.c`](../src/core/registry.c); add the sources to `LIB_SRCS` in the `Makefile`.
3. **Keep the write code out of login code.** `pam_fpal.so` and `fpald` link a copy of the library built *without* `FPAL_ENABLE_ENROLL`, and `make test` fails if write support got into it. Put everything that sends a store or delete command inside `#ifdef FPAL_ENABLE_ENROLL`, and list the **non-static** functions and tables that exist only there in `src/drivers/<name>/readonly.symbols` (see the Egis file). Static functions do not show up in `nm`, so make the command builders external. Without that file nothing checks your driver.
4. **Tests, no hardware needed:**
   - exact-byte tests for the frames you build and parse, with made-up ids, never ones from a real device;
   - drive the whole driver through a scripted transport ([`tests/mock_transport.c`](../tests/mock_transport.c), see `tests/test_egis_moc.c`) or a small simulated chip like `fake_moc`;
   - run the conformance checks on it (`tests/test_conformance.c` shows how: `driver_lint()` for the table rules, then open/close balance, list, identify, cancel, garbage input, and the enroll and delete promises above);
   - add a hook for your parsers to `tests/fuzz.c`.
5. **Document the protocol** in `docs/protocol-<name>.md`, and mark what you verified on real hardware and what comes only from reading libfprint.
6. **Hardware check** (needs root, so the owner runs it):
   ```sh
   sudo FPAL_TRACE=1 fpal info
   sudo FPAL_TRACE=1 fpal list
   sudo FPAL_TRACE=1 fpal identify
   ```
   `scripts/capture-trace.sh` records and compares such runs. Trace files contain real template ids: never commit them and do not paste them publicly.
7. **Chip writes are never tried without the owner's go-ahead.** Start with `fpal enroll --dry-run` (every step except the final store). For a real enroll and delete use a throwaway finger and an id that starts with `FPAL-`; never delete a template another system made.

## 5. Checklist

- [ ] `fpal report` output of the sensor in the issue or PR
- [ ] `src/drivers/<name>/` with `open/close/list/identify`, write ops only under `FPAL_ENABLE_ENROLL`
- [ ] ids go through `fpal/id.h`; at least 16 bytes; `max_templates` and `new_id_len` set
- [ ] registered in `registry.c` and `LIB_SRCS`
- [ ] `readonly.symbols` lists the write-only external symbols
- [ ] exact-byte tests, a driver-level test, conformance checks, a fuzz hook
- [ ] `make test` and `make ENROLL=0 test` both pass
- [ ] `docs/protocol-<name>.md`
- [ ] a trace from the real sensor
- [ ] README: sensor and its verification status (verified on hardware, or "from the libfprint protocol, not verified")
