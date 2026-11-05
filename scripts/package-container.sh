#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Build the real package for another distribution in a throw-away container
# and check what comes out:
#
#   scripts/package-container.sh debian|ubuntu|arch
#
#   debian, ubuntu  dpkg-buildpackage -> .deb, lintian (warnings only), installs
#                   it and runs "fpal setup" with the real pam-auth-update
#   arch            makepkg as a normal user -> .pkg.tar.zst, installs it
#
# In both, the login code in the package (pam_fpal.so, fpald) must pass the
# read-only check. Nothing on the host is changed.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
engine=${CONTAINER_ENGINE:-$(command -v podman || command -v docker || true)}
[ -n "$engine" ] || { echo "package-container: neither podman nor docker found" >&2; exit 2; }

case ${1:-} in
debian) image=docker.io/library/debian:stable ;;
ubuntu) image=docker.io/library/ubuntu:24.04 ;;
arch) image=docker.io/library/archlinux:latest ;;
*) echo "usage: $0 debian|ubuntu|arch" >&2; exit 2 ;;
esac

snap=$(mktemp)
trap 'rm -f "$snap"' EXIT
(cd "$root" && git ls-files -z -co --exclude-standard | tar --null -T - -cf "$snap")

case $1 in
debian | ubuntu)
	script='
apt-get update -qq >/dev/null
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq build-essential debhelper dpkg-dev \
	libpam0g-dev lintian ca-certificates >/dev/null
mkdir -p /src && tar -xf /src.tar -C /src
ver=$(sed -n "s/#define FPAL_VERSION \"\(.*\)\"/\1/p" /src/include/fpal/config.h)
mkdir -p /build && cp -r /src /build/fpal-$ver && cd /build/fpal-$ver
cp -r packaging/debian debian
echo "--- dpkg-buildpackage"
dpkg-buildpackage -b -us -uc >/tmp/build.log 2>&1 || { tail -40 /tmp/build.log; exit 1; }
tail -3 /tmp/build.log
deb=$(ls /build/fpal_*.deb)
echo "--- contents of $(basename $deb)"
dpkg-deb -c "$deb" | awk "{print \$1, \$6}"
dpkg-deb -f "$deb" Depends

echo "--- the login code in the package cannot write to the sensor"
mkdir /tmp/x && dpkg-deb -x "$deb" /tmp/x
sh scripts/check-readonly.sh /tmp/x/usr/lib/*/security/pam_fpal.so /tmp/x/usr/libexec/fpald

echo "--- lintian (warnings only)"
lintian --no-tag-display-limit "$deb" 2>&1 | head -30 || true

echo "--- install it"
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$deb" >/dev/null
fpal version
test -f /usr/share/pam-configs/fpal
cp /etc/pam.d/common-auth /tmp/before
fpal setup --no-services
grep -q pam_fpal.so /etc/pam.d/common-auth
fpal setup --revert --no-services
cmp /etc/pam.d/common-auth /tmp/before
echo "--- '"$1"': package builds, passes the checks, installs, setup and revert work"
'
	;;
arch)
	script='
pacman -Syu --noconfirm --quiet base-devel pam >/dev/null
mkdir -p /src && tar -xf /src.tar -C /src
ver=$(sed -n "s/#define FPAL_VERSION \"\(.*\)\"/\1/p" /src/include/fpal/config.h)
useradd -m builder
mkdir /home/builder/pkg
# the release tarball: top directory fpal-VERSION, as "make dist" makes it
tar -C /src --transform "s,^\.,fpal-$ver," -czf /home/builder/pkg/fpal-$ver.tar.gz .
# build from the local tarball instead of the download URL
sed "s|^source=.*|source=(\"fpal-\$pkgver.tar.gz\")|" /src/packaging/arch/PKGBUILD >/home/builder/pkg/PKGBUILD
chown -R builder /home/builder/pkg
echo "--- makepkg"
su builder -c "cd ~/pkg && makepkg -f --noconfirm" >/tmp/build.log 2>&1 || { tail -40 /tmp/build.log; exit 1; }
tail -3 /tmp/build.log
pkg=$(ls /home/builder/pkg/fpal-*.pkg.tar.zst | grep -v debug | head -n1)
echo "--- contents of $(basename $pkg)"
pacman -Qlp "$pkg" | grep -v "/$" | sed "s/^fpal //"

echo "--- the login code in the package cannot write to the sensor"
mkdir /tmp/x && tar -C /tmp/x -xf "$pkg"
sh /src/scripts/check-readonly.sh /tmp/x/usr/lib/security/pam_fpal.so /tmp/x/usr/lib/fpal/fpald

echo "--- install it"
pacman -U --noconfirm "$pkg" >/dev/null
fpal version
echo "--- fpal setup (no authselect or pam-auth-update here: only prints)"
fpal setup | head -5
echo "--- arch: package builds, passes the checks and installs"
'
	;;
esac

echo "=== package $1 ($image)"
"$engine" run --rm --security-opt label=disable -v "$snap:/src.tar:ro" "$image" \
	sh -eu -c "$script"
