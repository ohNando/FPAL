# Security

## Reporting a problem

Please report a vulnerability **privately**: on GitHub open the *Security* tab of the repository and choose *Report a vulnerability*. Do not open a public issue for it. A fix goes into the next release; only the latest release is supported.

## What FPAL protects, and what it does not

A fingerprint here **complements the password, it does not replace it**. The sensor is a Match-on-Chip device: it keeps the templates and does the matching itself, and the host learns only the 32 byte id of the template that matched. That sets the limits:

- Someone with **physical access to the sensor's USB lines** (or a malicious device pretending to be the sensor) could send the "matched" answer without a finger. Ids travel over USB in clear text, so they are not secrets. PAM therefore uses the fingerprint as `sufficient` and the password stays a fallback. **Do not use it for disk encryption (LUKS)**, and treat a login by fingerprint as a convenience.
- A fingerprint login does not unlock GNOME Keyring, because no password is typed.
- Nothing is secret in memory: templates never leave the chip, and the bindings and names on disk (`/var/lib/fpal`) hold only ids and labels.
- Only the sensor models and the Fedora / GNOME combination named in the README have been tested.

## How the code is kept safe

- **Only root opens the sensor.** There is no udev rule that would let ordinary programs send raw commands to it.
- **Login code cannot write to the sensor.** `pam_fpal.so` and `fpald` link a build of the library without the commands that store and delete fingerprints, and `make test` checks the binaries for them (`scripts/check-readonly.sh`). `make ENROLL=0` leaves those commands out of `fpal` as well.
- **Writing is deliberate.** `fpal enroll` and `fpal delete` need `--allow-chip-write` (or `--dry-run`) and a typed confirmation, delete exactly one fingerprint, read the sensor's list back to check, and refuse fingerprints fpal did not create unless told otherwise.
- **`fpald` does little.** It only tells GNOME that a reader exists. It never opens the sensor, refuses every claim, verify, enrol and delete call, and runs in a systemd sandbox without capabilities. Oversize or malformed D-Bus messages from any local user are dropped without stopping it.
- **The PAM module ignores the caller's environment.** `FPAL_TRACE` and `FPAL_DEBUG` have no effect inside it, because programs such as sudo run it with an environment the user controls.
- **Files are trusted only when they should be.** Bindings and names must be owned by root with no group or other write bit, are opened without following symlinks, and are written atomically. Text that a USB device or a name file can influence is checked before it reaches a terminal: names must be well formed UTF-8 without control, invisible or direction changing characters, and strings from the device are reduced to printable ASCII.
- **No dependencies at runtime** beyond libc and libpam. The D-Bus client is part of the project and is only a few hundred lines.
- **Builds are hardened** (PIE, full RELRO, immediate binding, non-executable stack, stack protector, stack clash protection), and the test suite runs under AddressSanitizer, UndefinedBehaviorSanitizer and the leak checker in CI.
- **The parsers are fuzzed.** `make fuzz` feeds mutated D-Bus messages, sensor responses, key presses and config files to the code that reads them, with every input next to an inaccessible page so that a single byte of over-read faults. CI runs it on every push.

## Verifying a download

Each release lists SHA-256 checksums in `SHA256SUMS`. Release packages are not signed yet.
