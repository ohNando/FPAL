# Sensors FPAL does not drive yet

FPAL's goal is to serve as many laptops as it can. Today it drives **Match-on-Chip sensors on USB**. Other kinds of sensors need new pieces; this page says which kind yours is, what is missing, and how far each effort has got. **Nothing on this page is promised for a date**, and each step ends in something that can be released and tested.

## Which kind is my sensor?

Run `fpal report --markdown` (no root needed). It lists USB sensors and sensors that ACPI describes (the latter from the release after 0.7.1). The three kinds:

| Kind | How it shows up | Status |
|---|---|---|
| **USB, Match-on-Chip** (templates live on the chip) | `fpal report` shows a USB device whose libfprint driver is marked Match-on-Chip | **Supported** if FPAL has a driver for the id (Egis today). For the other Match-on-Chip families (Goodix, Synaptics, Elan, FPC, ...) a driver is possible and needs an owner to test it: see [adding-a-sensor.md](adding-a-sensor.md). |
| **USB, image sensor** (the computer matches) | USB device, libfprint driver marked image sensor | **Not yet.** Needs matching on the computer and a store for templates on the computer's disk, with its own security model. Planned after the next kind. |
| **Not on USB** (ACPI, SPI, ...) | `fpal report` shows an "ACPI device ..." block | **Not yet.** Needs a new way to reach the sensor first. In progress for one sensor, below. |

## Goodix GXFP5130 (some Huawei MateBook laptops)

What it is, as far as other people's projects report (see [protocol-gxfp5130.md](protocol-gxfp5130.md) for the details and the sources, **not verified by FPAL**):

- It sits behind the laptop's Embedded Controller. ACPI describes a memory window ("mailbox") and three GPIO lines; there is no USB or PCIe device.
- A community kernel module (not in the mainline kernel) exposes this as `/dev/gxfp`. On top of it, community software reads fingerprint **images** and matches them on the computer. Image capture is reported to work on the MateBook 14 2024; matching is reported as unverified there.
- Whether the sensor can also store and match templates itself (which would make it an ordinary Match-on-Chip device for FPAL) is unknown and is one of the first things to test.

How FPAL gets there, in order. Each step is only started after the one before it worked on real hardware:

1. **Recognise it.** `fpal report` names the sensor and says why it is not supported. *(done on the development branch: this page and the report; not released yet.)*
2. **Find the way in.** Read the machine's ACPI description, then decide between the community kernel module (fast, but it is outside code that FPAL does not control), reaching the mailbox with kernel interfaces that already exist, or a small module of FPAL's own. Goal: as little outside software as possible.
3. **Talk to the sensor.** A read-only probe: open the channel and ask for the firmware version. No key and no TLS are needed for that.
4. **Get data from it.** The key exchange with the sensor (TLS with a pre-shared key, built on the kernel's own crypto interface instead of a library) and an image capture tool. On a machine that also runs Windows, FPAL will **use the key Windows already set up** instead of writing a new one into the sensor, so that both systems keep working.
5. **Is it Match-on-Chip?** Ask the sensor, read-only, whether it holds templates. If yes it becomes a normal FPAL driver. If no, step 6.
6. **Match on the computer.** Template store and a matcher written for FPAL, with a measured false-accept rate before it is ever used for login.

If you own such a laptop and want to help: run [`scripts/sensor-info.sh`](../scripts/sensor-info.sh) with `sudo` (it only reads, needs no FPAL build, and leaves out serial numbers and keys; with `acpica-tools` installed it also decodes the ACPI description; without it, it saves the raw DSDT and SSDT tables, never the MSDM table that holds the Windows licence key), read the file it writes, and attach it to a "New sensor report" issue. Say whether Windows is installed next to Linux.

## Elan SPI sensors

`fpal report` also recognises the ACPI ids `ELAN7001` and `ELAN70A1` (libfprint's `elanspi` driver; they are image sensors on SPI). They would follow the same path as the GXFP5130 and need an owner.
