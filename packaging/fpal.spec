Name:           fpal
Version:        0.1.0
Release:        1%{?dist}
Summary:        Fingerprint Auth for Linux
License:        Apache-2.0
URL:            https://github.com/ohNando/fpal
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  pam-devel
Requires:       pam

%description
FPAL is a from-scratch userspace fingerprint driver, command line tool and PAM
module for Match-on-Chip fingerprint sensors (currently EgisTec / LighTuning
1c7a:0587). It talks to the sensor through usbdevfs, needs no libusb or
libfprint, and never deletes templates stored on the sensor.

Installing the package does not change the PAM stack. To use the fingerprint
for sudo and su, run /usr/share/fpal/authselect-fpal.sh install (Fedora /
RHEL). See /usr/share/doc/fpal/usage.md.

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
%make_install PREFIX=%{_prefix} PAMDIR=%{_libdir}/security \
    DOCDIR=%{_docdir}/%{name} DATADIR=%{_datadir}/%{name}

%preun
if [ $1 -eq 0 ] && command -v authselect >/dev/null 2>&1 &&
   authselect current --raw 2>/dev/null | grep -q '^custom/fpal'; then
    echo "fpal: the authselect profile custom/fpal is still active." >&2
    echo "fpal: run '/usr/share/fpal/authselect-fpal.sh revert' to go back" >&2
fi
exit 0

%files
%license LICENSE
%{_sbindir}/fpal
%{_libdir}/security/pam_fpal.so
%{_datadir}/%{name}/
%{_docdir}/%{name}/

%changelog
* Sat Oct 31 2026 ohNando <emirhanbuldurucu12@gmail.com> - 0.1.0-1
- First release
