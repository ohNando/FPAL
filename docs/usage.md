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
| `sudo fpal list` | Template IDs stored on the sensor |
| `sudo fpal identify [-t ms]` | Touch the sensor and show the matching ID |
| `sudo fpal bind [-t ms] USER` | Bind the touched finger to a Linux user |
| `sudo fpal bindings USER` | Show the IDs bound to a user |
| `sudo fpal unbind USER` | Remove the bindings. Deletes only the file on the computer, the sensor is untouched |
| `fpal version` | Show the version |

Fingers enrolled in Windows Hello work on Linux as they are. Enrolling from Linux exists too, but it writes to the sensor, so it is not part of the normal build (see below). Bindings are stored in `/var/lib/fpal/<user>` (root, `0600`).

## Enrolling from Linux (optional)

Storing a new fingerprint is the only thing in FPAL that writes to the sensor. It adds a template and never touches, replaces or deletes the existing ones. Three locks guard it:

1. Build it in on purpose: `make ENROLL=1` (output goes to `build-enroll/`). Packages and the default build cannot enroll.
2. Ask for it: `--allow-chip-write`.
3. Confirm by typing `yes` at a terminal.

```sh
make ENROLL=1
sudo build-enroll/fpal enroll --dry-run $USER              # every step, nothing stored
sudo build-enroll/fpal enroll --allow-chip-write $USER     # 20 scans, then stored and bound
```

`--dry-run` is the way to try it first: it goes through the whole scan sequence, skips the two commands that store the template, resets the sensor and checks that the list on the sensor did not change. The sensor keeps at most 10 fingerprints; a finger that is already enrolled is refused.

## Login with sudo and su

```sh
sudo fpal bind $USER
sudo scripts/authselect-fpal.sh install   # Fedora / RHEL (authselect)
```

The script masks `fprintd`, selects an authselect profile called `custom/fpal` and starts `fpald` for GNOME (see [gnome.md](gnome.md)); `revert` undoes it. For the PAM options (`timeout=`, `retries=`, `quiet`, `debug`), the messages and safe testing steps see [pam.md](pam.md).

## Troubleshooting

`FPAL_TRACE=1` prints every USB packet sent and received as hex, and `FPAL_DEBUG=1` enables verbose messages:

```sh
sudo FPAL_TRACE=1 fpal identify
```

| Symptom | Cause and fix |
|---|---|
| `device or resource busy` | Another process (e.g. `fprintd`) holds the sensor: `sudo systemctl mask --now fprintd` |
| `permission denied` | The command must run as root: `sudo fpal ...` |
| `NO MATCH` | The finger is not enrolled on the sensor. Enrol it in Windows Hello first |
| sudo does not ask for a fingerprint | No finger is bound to the user (`sudo fpal bindings $USER`) or the authselect profile is not selected (`authselect current`) |
