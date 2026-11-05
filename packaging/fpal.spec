Name:           fpal
Version:        0.6.1
Release:        1%{?dist}
Summary:        Fingerprint Auth for Linux
License:        Apache-2.0
URL:            https://github.com/ohNando/fpal
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  pam-devel
BuildRequires:  systemd-rpm-macros
Requires:       pam
Requires:       dbus-common
%{?systemd_requires}

%description
FPAL is a from-scratch userspace fingerprint driver, command line tool and PAM
module for Match-on-Chip fingerprint sensors (currently EgisTec / LighTuning
1c7a:0587). It talks to the sensor through usbdevfs, needs no libusb or
libfprint, and never deletes templates stored on the sensor. The fpald service
tells GNOME that a sensor exists so the login and lock screens offer it.

Installing the package does not change the PAM stack and does not start
fpald. To use the fingerprint for sudo, su and GNOME, run "sudo fpal setup"
(Fedora / RHEL), and "fpal status" to check it. See
/usr/share/doc/fpal/usage.md.

%prep
%autosetup

%build
# Distribution flags arrive through the environment (CFLAGS, LDFLAGS); the
# Makefile appends its own warning flags. -Werror is dropped so a newer
# compiler cannot break the package build over a warning.
%make_build CFLAGS_EXTRA=-Wno-error

%check
make test CFLAGS_EXTRA=-Wno-error

%install
%make_install PREFIX=%{_prefix} SBINDIR=%{_sbindir} PAMDIR=%{_libdir}/security \
    DOCDIR=%{_docdir}/%{name} DATADIR=%{_datadir}/%{name} \
    LIBEXECDIR=%{_libexecdir} UNITDIR=%{_unitdir} \
    POLICYDIR=%{_datadir}/dbus-1/system.d MANDIR=%{_mandir}

%post
%systemd_post fpald.service

%postun
%systemd_postun_with_restart fpald.service

%preun
%systemd_preun fpald.service
if [ $1 -eq 0 ] && command -v authselect >/dev/null 2>&1 &&
   authselect current --raw 2>/dev/null | grep -q '^custom/fpal'; then
    echo "fpal: the authselect profile custom/fpal is still active." >&2
    echo "fpal: run 'fpal setup --revert' to go back" >&2
fi
exit 0

%files
%license LICENSE
%{_sbindir}/fpal
%{_libdir}/security/pam_fpal.so
%{_libexecdir}/fpald
%{_unitdir}/fpald.service
%{_datadir}/dbus-1/system.d/fpald.conf
%{_mandir}/man8/fpal.8*
%{_mandir}/man8/pam_fpal.8*
%{_mandir}/man8/fpald.8*
%{_datadir}/%{name}/
%{_docdir}/%{name}/

%changelog
* Thu Nov 05 2026 ohNando <emirhanbuldurucu12@gmail.com> - 0.6.1-1
- Security: a local user could stop fpald with one oversize D-Bus message, which
  removed the fingerprint option from the GNOME screens until the next boot
- fpald exits with a non-zero status when the bus connection is lost, so
  systemd starts it again
- pam_fpal ignores FPAL_TRACE and FPAL_DEBUG from the caller's environment
- Stricter fingerprint names, hardened link flags, a fuzzer and sanitizer CI

* Mon Nov 02 2026 ohNando <emirhanbuldurucu12@gmail.com> - 0.6.0-1
- Add fpal manage, a full-screen list to rename, delete and add fingers with
  the arrow keys; deleting and adding ask for a typed confirmation

* Mon Nov 02 2026 ohNando <emirhanbuldurucu12@gmail.com> - 0.5.0-1
- Enrolling and deleting a fingerprint are part of the normal build, guarded by
  --allow-chip-write and a typed confirmation; the PAM module and fpald still
  cannot write to the sensor
- Add fpal setup and fpal status, a table for fpal list, and man pages

* Mon Nov 02 2026 ohNando <emirhanbuldurucu12@gmail.com> - 0.4.0-1
- Add names for fingerprints (fpal label)
- Add deleting one fingerprint, only in builds made with ENROLL=1
- Packages are built without enrolling and deleting

* Sun Nov 01 2026 ohNando <emirhanbuldurucu12@gmail.com> - 0.3.0-1
- Add optional enrolling from Linux (only in builds made with ENROLL=1)
- Packages are built without it and cannot write to the sensor

* Sun Nov 01 2026 ohNando <emirhanbuldurucu12@gmail.com> - 0.2.0-1
- Add fpald, so the GNOME login and lock screens offer the fingerprint
- Add a small D-Bus implementation, a systemd unit and a bus policy file

* Sat Oct 31 2026 ohNando <emirhanbuldurucu12@gmail.com> - 0.1.0-1
- First release
