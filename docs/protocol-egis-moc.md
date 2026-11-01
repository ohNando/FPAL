# Egis MoC USB protocol

This document describes the USB protocol of EgisTec / LighTuning Match-on-Chip sensors. The initial source was libfprint's `egismoc` driver, which in turn was derived from captured USB traffic of the Windows driver. Everything here was verified on real hardware:

- Device: `1c7a:0587` ETU905A88-E (ASUS Zenbook 14 UX3402VA)
- Firmware: `9050.6.4.67`

A ✅ marks information confirmed on this device with `FPAL_TRACE=1` dumps.

## USB interface

| Endpoint | Type | Use |
|---|---|---|
| EP0 | Control | Open sequence |
| `0x02` | Bulk OUT | Sending commands |
| `0x81` | Bulk IN | Reading responses |
| `0x83` | Interrupt IN | Finger touch event |

Interface `0`, class `0xFF` (Vendor Specific).

## Open sequence ✅

1. Claim interface 0 to check that no other process holds the sensor, then release it.
2. USB reset.
3. Claim interface 0 again.
4. Control IN requests:

| # | Type | bRequest | wIndex | wLength | Meaning |
|---|---|---|---|---|---|
| 1 | Vendor | `0x20` | 4 | 16 | Microsoft OS descriptor: Extended Compat ID header |
| 2 | Vendor | `0x20` | 4 | 40 | Same, full form: `"WINUSB"` compatible ID |
| 3 | Standard | GET_STATUS | 0 | 2 | `01 00` (self-powered) |
| 4 | Standard | GET_STATUS | 0 | 2 | `01 00` |
| 5 | Vendor | `0x52` | 0 | 8 | `0a 0e 00 00 00 00 00 00` (meaning unknown) |

The first two requests are not sensor specific. Windows uses this standard mechanism to bind the WinUSB driver to the device automatically; `0x20` is the device's Microsoft OS descriptor "vendor code".

## Frame format ✅

```
Command   (host → sensor):  45 47 49 53 00 00 00 01 | CK CK | payload...
Response  (sensor → host):  53 49 47 45 00 00 00 01 | ck ck | LEN(4, BE) | body... | SW1 SW2
```

- `LEN` is the total length of the body plus the status word (SW) ✅. libfprint took the `0D` byte in the firmware response for a carriage return; it is actually the length field (13 = 11 characters of version + 2 bytes of SW).
- A command payload starts with a 4 byte big-endian length, followed by an APDU-like body: `50 INS P1 P2 [00 Lh Ll]`.
- Status words:
  - `90 00` success.
  - `90 04` no match.
  - `91 00` answer to the "arm identify" command ✅.

### Checksum ✅

Commands and responses use the same checksum:

1. With the checksum field set to `00 00`, sum the frame as big-endian 16 bit words. If the length is odd, compute as if one zero pad byte were appended (it is not sent).
2. `CK = 0xFFFF − (sum mod 0xFFFF)`.

The sum of the whole frame, checksum included, is therefore `0 (mod 0xFFFF)`. The driver rejects responses whose checksum does not match.

## Commands

| Command | Payload | Observed response |
|---|---|---|
| Firmware version | `00 00 00 07 50 7F 00 00 00 00 0C` | body: ASCII version (`9050.6.4.67`) ✅ |
| Template list | `00 00 00 07 50 19 04 00 00 01 40` | body: N × 32 byte IDs ✅ |
| Sensor reset | `00 00 00 04 50 1A 00 00` | SW `90 00` ✅ |
| Arm identify | `00 00 00 04 50 17 01 01` | SW `91 00` ✅ |
| Sensor check | `00 00 00 04 50 17 02 00` | SW `90 00` ✅ |
| Match ("check") | below | below ✅ |

**The delete command (`INS 0x18`) is intentionally not defined in this project.**

### Match command ✅

```
LEN(4) | 50 17 03 00 00 | IDLEN(2) | 32 × 00 | ID_1 ... ID_n | 00 40
```

- `IDLEN = (n + 1) × 32`: the leading 32 zero byte "separator" is counted too.
- `LEN = IDLEN + 9`.

Response:
- **Match:** SW `90 00`, 64 byte body.
  - The first 32 bytes are unknown metadata. Example: `14 00 00 00 a0 00 c0 f0 03 00 00 00 03 00 00 00 58 ac ...`. It may be a quality value or a score.
  - The next 32 bytes are the matched ID (body offset 32, absolute offset 46) ✅.
- **No match:** SW `90 04`.

## Finger event (interrupt `0x83`) ✅

A 64 byte read request returns 63 bytes (libfprint noted this one byte shortfall too). The content is itself a complete SIGE frame:

```
53 49 47 45 00 00 00 01 2d 1e 00 00 00 31 | 17 01 20 ff ff ... ff | 90 00 90 00
```

The body starts with `17 01`, an echo of the `INS P1` bytes of the "arm identify" command. The driver does not parse this content; it only waits for the event.

## Enrolling

Taken from libfprint's `egismoc` driver (`libfprint/drivers/egismoc`, 1.94.100). The `0587` uses 20 scans (`EGIS_FLAG_ENROLL_STAGES_20`), other family members 10. These commands are compiled out of the PAM module and `fpald` (and of `fpal` itself when built with `make ENROLL=0`).

A ✅ marks what a full `fpal enroll --dry-run` on 1c7a:0587 (fw `9050.6.4.67`, 20 scans, 6 of them retried) showed with `FPAL_TRACE=1`. Commit start and store were never sent in that run. A real `fpal enroll` on the same device then stored a new template: the sensor's list grew from 1 to 2 entries, the new id was there and the existing one untouched ✅. The status words of those two commands were not traced.

| Command | Payload | Answer |
|---|---|---|
| Arm enrol | `00 00 00 04 50 17 01 00` | SW `91 00` ✅ (like arm identify) |
| Enrol start | `00 00 00 07 50 16 01 00 00 00 20` | SW `90 00` with a 32 byte body ✅ (random looking, not an existing id; meaning unknown, libfprint ignores it) |
| Start capture | `00 00 00 04 50 16 02 01` | SW `91 00` ✅ |
| Read capture | `00 00 00 07 50 16 02 02 00 00 02` | see below ✅ |
| Commit start | `00 00 00 07 50 16 05 00 00 00 20` | SW ✅ (worked) |
| **Store** | `00 00 00 27 50 16 03 00 00 00 20` + 32 byte id | SW ✅ (template stored) |

Capture answers: `90 00` with a 2 byte body is an accepted scan ✅, `64 91` with a 2 byte body means off centre, `64 xx` with no body means a dirty sensor; anything else is retried. On the test finger the sensor also answered `64 93` and `64 95` (2 byte body) for unreadable scans ✅, handled as "lift and touch again". Retries do not count as stages.

```
list → (full? id taken?) → reset → arm enrol → [finger event] → sensor check
     → check (same command as identify; 90 04 = finger is new ✅, 90 00 = already enrolled)
     → enrol start
     → N × ( reset → start capture → [finger event] → read capture )
     → commit start → store(id) → reset
```

Everything up to the last scan lives in the sensor's working memory; **only the store command writes a template**, and the project never sends a delete. FPAL ids are 32 printable bytes: `FPAL-` followed by 27 random hex digits. `fpal enroll --dry-run` runs the whole sequence except commit start and store, then resets the sensor.

## Deleting

The command libfprint and the Windows driver use to remove templates, from libfprint 1.94.100 (`egismoc_get_delete_cmd`):

```
00 00 | BE16(7 + 32n) | 50 18 04 00 00 | BE16(32n) | id_1 ... id_n
```

Success is a bare answer: `00 00 00 02 90 00` (length 2, no body, SW `90 00`). Verified on 1c7a:0587: deleting the template FPAL had enrolled left the list at exactly the other template ✅. The chip accepts a list of ids, and libfprint sends all of them to "clear" the sensor. **FPAL only ever builds the command for n = 1** (`egis_build_delete()` takes a single id), and after the answer reads the template list back to check that exactly that id is gone and every other one is still there. Like the enrol commands it is compiled out of the PAM module, `fpald` and read-only builds.

## Identify flow ✅

```
list → reset → arm identify → [finger event] → sensor check → match → reset
```

The driver checks that the matched ID is actually present in the `list` result. If it is not, the offset is assumed to be wrong for this firmware and an error is returned.

Fingers enrolled in Windows appear in the list and can be matched from Linux ✅.
