# Installation and usage

## Packages

Packaging files live in `packaging/`: an RPM spec (`packaging/fpal.spec`, Fedora / RHEL), Debian files (`packaging/debian/`) and an Arch `PKGBUILD` (`packaging/arch/`). Only the RPM is tested. Installing a package never changes the PAM stack; the helper script is installed as `/usr/share/fpal/authselect-fpal.sh` (use it instead of `scripts/authselect-fpal.sh` below).

To build the RPM from a checkout:

```sh
sudo dnf install rpm-build gcc make pam-devel
make dist
mkdir -p ~/rpmbuild/SOURCES && cp build/fpal-*.tar.gz ~/rpmbuild/SOURCES/
rpmbuild -ba packaging/fpal.spec
```

For Debian, copy `packaging/debian` to `debian` in the source tree and run `dpkg-buildpackage -us -uc`.

## Build and install

You need a C compiler, `make` and the `libpam` headers:

```sh
sudo dnf install gcc make pam-devel      # Fedora
sudo apt install gcc make libpam0g-dev   # Debian / Ubuntu (untested)

make            # build/fpal, build/fpald, build/pam_fpal.so
make test       # unit tests, no hardware needed
sudo make install
```

`make install` puts the `fpal` command under `/usr/local/sbin`, `pam_fpal.so` into the distribution's PAM directory, and the GNOME service (`fpald`, its systemd unit and D-Bus policy) under `/usr/local/libexec`, `/usr/local/lib/systemd/system` and `/etc/dbus-1/system.d`. The paths can be changed with `PREFIX=`, `PAMDIR=`, `LIBEXECDIR=`, `UNITDIR=` and `POLICYDIR=`; `make install-fpald` installs only the service.

Every tunable value (timeouts, paths, limits) lives in `include/fpal/config.h` and can be overridden at build time:

```sh
make CFLAGS_EXTRA='-DFPAL_PAM_TIMEOUT_MS=5000'
```

## Commands

Only root can open the sensor. This is on purpose: with a world-accessible udev rule, every program the user runs could send raw commands to the sensor.

| Command | What it does |
|---|---|
| `sudo fpal info` | Sensor, driver, serial number and firmware version |
| `sudo fpal manage` | A full-screen list: pick a finger with the arrow keys, `r` renames it, `d` deletes it, `n` adds a new finger, `q` quits |
| `sudo fpal list [--raw]` | Fingers on the sensor as a table (number, name, kind, whether it is yours, id). In a pipe or with `--raw` it prints one plain line per finger with the full id, for scripts |
| `sudo fpal identify [-t ms]` | Touch the sensor and show the matching ID |
| `sudo fpal bind [-t ms] USER` | Bind the touched finger to a Linux user |
| `sudo fpal bindings USER` | Show the IDs bound to a user |
| `sudo fpal unbind USER` | Remove the bindings. Deletes only the file on the computer, the sensor is untouched |
| `sudo fpal label FINGER [NAME\|--clear]` | Show, set or remove the name of a finger |
| `fpal status [USER]` | Is fingerprint login set up? Shows the sensor, the fingers, the PAM and GNOME state, and what to do if something is missing (run it with `sudo` to see the fingers and bindings) |
| `sudo fpal setup [--revert]` | Make sudo, su and the GNOME screens use the fingerprint, or go back |
| `fpal version` | Show the version |

Fingers enrolled in Windows Hello work on Linux as they are. Enrolling from Linux, and deleting a finger, exist too, but they write to the sensor, so they are not part of the normal build (see below). Bindings are stored in `/var/lib/fpal/<user>` (root, `0600`).

## The manage screen

`sudo fpal manage` is the easy way to look after your fingers. It shows the same table as `fpal list` and you move through it with the arrow keys (or `j` and `k`):

| Key | What it does |
|---|---|
| `↑` `↓`, `j` `k`, Home, End | Pick a finger |
| `r` | Rename it (the editor starts with the current name; Enter saves, an empty name removes it) |
| `d` | Delete it. You are asked to type `yes` first; for a finger fpal did not create (most likely from Windows Hello) you must type `DELETE` |
| `n` | Add a new finger bound to you: type `yes`, touch the sensor for the scans, then give it a name or press Enter to skip |
| `q`, Esc | Quit |

Nothing is deleted or stored by a single key press: `d` and `n` only open the question, and anything other than the exact word cancels it (Esc cancels too). It needs a terminal; in scripts use `fpal list` and the other commands. A read-only build (`make ENROLL=0`) leaves out `d` and `n`.

## Naming fingers

The sensor stores only an opaque 32 byte id per finger, so names are kept on this computer in `/var/lib/fpal/.labels` (root, `0600`). They are visible to this installation only; Windows and other systems do not see them.

```sh
sudo fpal list                                   # [0] ... [1] ...
sudo fpal label 0 "Windows finger"
sudo fpal label 1 "Linux finger"
sudo fpal label 1                                # show the name
sudo fpal label 1 --clear                        # remove it
```

A name is 1 to 48 bytes (UTF-8 is fine, so `"Sağ işaret"` works), with no control characters and no leading or trailing space. Anywhere a finger is asked for (`label`, `delete`; in `fpal manage` you just pick it) you can give its number, its exact name, or the start of its id (at least 8 hex digits or 6 characters of text such as `FPAL-7b51`). If that matches more than one finger, nothing happens.

## Enrolling and deleting from Linux

Storing a new fingerprint and deleting one are the only things in FPAL that write to the sensor. Enrolling adds a template and never touches, replaces or deletes the existing ones. Two locks guard both, so a typo cannot do it:

1. Ask for it: `--allow-chip-write` (or `--dry-run` to try without writing).
2. Confirm by typing `yes` at a terminal. Nothing can be confirmed from a script.

```sh
sudo fpal enroll --dry-run $USER              # every step, nothing stored
sudo fpal enroll --allow-chip-write $USER     # 20 scans, then stored and bound
```

The PAM module and `fpald` never write, whatever `fpal` can do: they are linked against a build of the library without the code that stores and deletes, and `make test` checks the binaries for it. If you want an `fpal` that cannot write either, build it with `make ENROLL=0` (output goes to `build-readonly/`).

`--dry-run` is the way to try it first: it goes through the whole scan sequence, skips the two commands that store the template, resets the sensor and checks that the list on the sensor did not change. The sensor keeps at most 10 fingerprints; a finger that is already enrolled is refused. Give it a name while enrolling with `--name`:

```sh
sudo fpal enroll --allow-chip-write --name "Linux finger" $USER
```

### Deleting one finger

```sh
sudo fpal delete --dry-run 1                 # show what would be deleted
sudo fpal delete --allow-chip-write 1        # then type yes
sudo fpal delete --allow-chip-write "Linux finger"
```

Deleting removes a fingerprint from the sensor for good and **cannot be undone**. It removes exactly the finger you picked: afterwards the sensor's list is read back, and if anything other than that one finger changed, the command reports an error. There is no way to delete several fingers or clear the sensor, on purpose. The finger is also dropped from every user's bindings and its name is forgotten.

A finger FPAL did not create (its id does not start with `FPAL-`, so it most likely comes from Windows Hello) is refused unless you add `--allow-foreign`, and then you must type `DELETE` instead of `yes`. Deleting such a finger removes it for every system that uses the sensor.

## Login with sudo and su

```sh
sudo fpal bind $USER
sudo fpal setup            # Fedora / RHEL (authselect), Debian / Ubuntu (pam-auth-update)
fpal status                # check that it is all in place
```

`fpal setup` masks `fprintd`, changes the PAM files the way the system does it (an authselect profile called `custom/fpal` on Fedora, the `fpal` profile of `pam-auth-update` on Debian and Ubuntu; elsewhere it only prints what to do) and starts `fpald` for GNOME (see [gnome.md](gnome.md)); `sudo fpal setup --revert` undoes it, and `fpal setup --dry-run` shows the steps without doing anything. It runs `platform.sh`, which is installed next to `fpal` (`/usr/share/fpal/`, or `scripts/` in a checkout). For the PAM options (`timeout=`, `retries=`, `quiet`, `debug`), the messages and safe testing steps see [pam.md](pam.md).

## Troubleshooting

`FPAL_TRACE=1` prints every USB packet sent and received as hex, each with the time in milliseconds since the first one (it shows where the time goes), and `FPAL_DEBUG=1` enables verbose messages:

```sh
sudo FPAL_TRACE=1 fpal identify
```

| Symptom | Cause and fix |
|---|---|
| `device or resource busy` | Another process (e.g. `fprintd`) holds the sensor: `sudo systemctl mask --now fprintd` |
| `permission denied` | The command must run as root: `sudo fpal ...` |
| `NO MATCH` | The finger is not enrolled on the sensor. Enrol it in Windows Hello first |
| sudo does not ask for a fingerprint | No finger is bound to the user (`sudo fpal bindings $USER`) or the authselect profile is not selected (`authselect current`) |
