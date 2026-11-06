# Goodix GXFP5130 (draft notes)

> **Status: notes from reading other people's projects. Nothing here has been verified on hardware by FPAL, and FPAL has no driver for this sensor yet.** Every statement is marked with where it comes from. This page will be corrected as each step is tried on a real machine.

The GXFP5130 is the fingerprint sensor of several Huawei MateBook laptops (reported for the MateBook 14 2024, GT 14 and X Pro 2024). Windows calls it `ACPI\GXFP5130`. It is **not a USB device**: it sits behind the laptop's Embedded Controller (EC), so none of FPAL's current USB code reaches it.

## Sources and licences

Written from reading the following community projects (all experimental, none affiliated with Goodix or Huawei). No code is copied; only facts about the hardware interface and the protocol are recorded here, and FPAL's own implementation will be written from scratch, as for the Egis sensor.

| Project | What it has | Licence as stated |
|---|---|---|
| [Void755/gxfpmoc](https://github.com/Void755/gxfpmoc) | kernel module, userspace library, tools, libfprint fork | MIT (repository "About") |
| [Metrohan/gxfp5130-linux](https://github.com/Metrohan/gxfp5130-linux) | packaging, docs, scripts around the same code | kernel GPL-2.0-only, userspace GPL-2.0-only, libfprint fork LGPL-2.1+ (its README) |

The licences disagree between the two projects, and the README of the second one says that **the origin and licence of the sensor configuration table are unverified**. FPAL will not ship anything that depends on that table until this is settled (see "Open questions").

## Layers

### 1. The mailbox (hardware, reported by the kernel module)

- ACPI device `GXFP5130:00`. Its `_CRS` holds one **memory window** (the mailbox, at least `0x1000` bytes, address differs per machine), two **GpioIo** pins and one **GpioInt**.
- The two output pins are, in `_CRS` order, **write-done** and **read-done**; the module cannot tell them apart by name. The input interrupt is **irq** (EC to CPU). Polarity comes from `_CRS`.
- Memory layout: **TX at offset `0x0`**, **RX at offset `0x200`**, accessed as **64-bit little-endian words** (TX payload is padded to a multiple of 8 bytes). Memory barriers separate writing the payload from the doorbell, and reading the RX window from acknowledging it.
- **Sending:** write the payload to TX, then pulse write-done: low 50 µs, high about 4000 µs, low 200 µs.
- **Receiving:** the irq fires; the host copies the RX window, then pulses read-done: high 50 µs, low 200 µs.
- The sensor itself is on the EC's internal SPI bus; the CPU never talks to it directly.
- No ACPI power methods (`_DSM`, `_PS0`) are called by the module, only suspend/resume handling.

How the community kernel module exposes this: a misc device `/dev/gxfp` (mode 0600, needs `CAP_SYS_RAWIO`, one opener at a time). `write()` sends one message (size at most `GXFP_TX_MAX`; it blocks for the whole write-done pulse, about 4.25 ms); `read()` returns exactly one record per call; `poll()` reports queued records; one ioctl flushes the receive queue. The kernel patch series for this module was withdrawn upstream, so it is a DKMS (out of tree) module.

### 2. Mailbox wrapper (8-byte header)

| Offset | Meaning |
|---|---|
| 0 | magic `0xF0` |
| 1-2 | length of the MP frame that follows, little-endian |
| 3 | checksum: byte 0 + byte 1 + byte 2, modulo 256 |
| 4-5 | sequence number, little-endian, incremented before each use |
| 6-7 | reserved, zero |
| 8... | the MP frame, then zero padding up to a multiple of 8 |

Limits: one transmit buffer is `0x200` (512) bytes, so a command can carry at most 500 payload bytes (`512 - 8 - 4`); receive reads are cut into chunks of at most `0xE00` (3584) bytes.

### 3. MP frame (4-byte header)

| Offset | Meaning |
|---|---|
| 0 | channel in the high nibble: `0xA` command (request and response), `0xB` TLS container, `0xC` asynchronous notice (low nibble zero) |
| 1-2 | payload length, little-endian (must be below `0x6C00`) |
| 3 | checksum: byte 0 + byte 1 + byte 2, modulo 256 |
| 4... | payload |

### 4. Goodix command frame (inside a command-channel payload)

| Offset | Meaning |
|---|---|
| 0 | command byte |
| 1-2 | `1 + payload length`, little-endian |
| 3... | payload (at most `0xFF00` bytes) |
| last | checksum: `0xAA - (sum of command, length bytes and payload) mod 256` |

The community parser does not reject a wrong checksum; it only records it. The framing may be related to what Goodix uses on its USB sensors; FPAL has not checked that, and it proves nothing about the command set.

### 5. Commands seen in the community headers

Names are the projects' own. **The meanings are guessed from names and unverified**; the risk column is FPAL's own caution, not something the projects state.

| Command | Id | FPAL's first impression |
|---|---|---|
| NOP | `0x01` | harmless |
| firmware version | `0xA8` | read-only, **first probe** |
| query MCU state | `0xAE` | read-only, 20-byte state (see below), **second probe** |
| read OTP | `0xA6` | read-only |
| register read | `0x82` | read-only |
| finger detection status | `0xDA` | read-only |
| finger detection down / up / mode | `0x32` `0x34` `0x36` | changes the sensor's detection mode |
| get image | `0x20` | captures an image |
| set idle | `0x70` | state change |
| navigation frames | `0x50` `0x52` | capture |
| trigger MCU state | `0xAF` | state change |
| ack | `0xB0` | |
| TLS server init / unlock / force unlock | `0xD0` `0xD4` `0xD5` | TLS session control |
| `0xD1`, sleep cleanup | `0xD1` `0xD2` | unknown |
| notify power state | `0x0E` | state change |
| reset device | `0xA2` | state change |
| **upload config to MCU** | `0x90` | **writes to the sensor, not to be used** |
| **set DAC** | `0x98` | **writes sensor hardware settings** |
| **production write MCU** | `0xE0` | **factory command, writes, never** |
| preset PSK read | `0xE4` | would return the TLS key: **secret**, never print or log |

FPAL's rule stays: nothing that writes is sent without `--allow-chip-write` and a typed confirmation, and no first probe sends anything but the read-only commands marked above.

### 6. MCU state (reply to `0xAE`, 20 bytes, community parser)

Fields by position: version; a raw byte; "POV image valid"; "TLS connected"; "locked"; two bytes of captured-frame count; six raw bytes; a byte with flags (PBA, TLS session, POV, low); five raw bytes; a raw byte; four raw bytes. The meanings of most bits are not documented.

### 7. Finger detection (FDT)

Status word, 16-bit little-endian: `0x0002` finger down, `0x0200` finger up, `0x0080` or `0x0082` "reverse" event. The community code keeps two 24-byte "base" tables (finger up and finger down) that it refreshes from sensor frames.

### 8. TLS (image data only)

- TLS 1.2 with a **pre-shared key**, over TLS records carried in MP frames of the TLS channel. **The roles are reversed: the CPU is the TLS server, the sensor is the TLS client.**
- PSK identity string `Client_identity`; PSK length 32 bytes; maximum plaintext record 16384 bytes.
- The community code names no cipher suite (it uses the library defaults for PSK), so **the suite is not known yet**; it will have to be read from the sensor's own ClientHello.
- Per the community docs, **commands travel in plaintext and only image data goes through TLS.**

### 9. The key (PSK)

- The sensor's MCU stores the key in a "provisioning blob" (`BB010002`). There is **no sign that the key is factory-fixed**.
- When the Windows driver sees a key mismatch it **generates a new key and uploads it to the sensor**, and caches it, sealed with Windows DPAPI, in `Goodix_Cache.bin`.
- The projects offer two ways: **(1)** extract the key from the Windows cache with a small Python tool, or **(2)** let a Linux tool generate a new key and **upload it to the sensor**.
- **Consequence for dual-boot machines:** with (2), Windows will not recognise the key on its next start and will replace it; Linux's copy is then wrong. Method (1) needs no write to the sensor and both systems keep the same key. FPAL will use (1) first and will treat any key upload as a chip write.
- The key is a secret: root-only file, never in logs, traces, issues or the repository. No documented recovery exists if a key upload goes wrong.

### 10. Images

The community tool saves a raw 16-bit grayscale image (PGM). Reported sizes: **64 x 80** on the MateBook 14 2024 (firmware `GF_GCC_EC_20068`), 176 x 54 on the MateBook GT 14 (`GF_GCC_EC_20061`). The sensor MCU also keeps frames it captured at boot ("POV").

### 11. Matching

The community libfprint fork matches on the host from images (an algorithm called SIGFM, using OpenCV). Whether the MCU can store templates and match on its own is **not stated anywhere**; the project name ("moc") and the shared Goodix framing make it a question worth testing read-only before building a host matcher.

## Reported status (community, 2026-11)

| Laptop | Image size | Reported |
|---|---|---|
| MateBook GT 14 | 176 x 54 | working |
| MateBook 14 2024 | 64 x 80 | "image capture works, matching unverified" |
| MateBook X Pro 2024 | not given | partly working |
| MateBook X Pro 2024 (`SIL6250`, a different chip) | | not working |

## Open questions (to settle on hardware or with the authors)

1. Is the mailbox reachable without the community kernel module (it is plain ACPI memory plus three GPIOs)? Decided from the machine's ACPI tables.
2. Which TLS cipher suite does the sensor offer?
3. Can the MCU list, enrol and match templates (a real Match-on-Chip mode), and do Windows Hello fingers live there?
4. Where does the sensor configuration table come from, and under which licence may it be used?
5. What exactly does the receive window contain per interrupt (record length and layout)?
