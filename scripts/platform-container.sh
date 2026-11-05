#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Run "fpal setup" with the REAL authselect or pam-auth-update inside a
# throw-away container, to see what it does to the PAM files:
#
#   scripts/platform-container.sh fedora|debian|ubuntu
#
# Services are left alone (--no-services; a container has no systemd). The
# container is removed afterwards; nothing on the host is changed.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
engine=${CONTAINER_ENGINE:-$(command -v podman || command -v docker || true)}
[ -n "$engine" ] || { echo "platform-container: neither podman nor docker found" >&2; exit 2; }

case ${1:-} in
fedora) image=docker.io/library/fedora:latest ;;
debian) image=docker.io/library/debian:stable ;;
ubuntu) image=docker.io/library/ubuntu:24.04 ;;
*) echo "usage: $0 fedora|debian|ubuntu" >&2; exit 2 ;;
esac

snap=$(mktemp)
trap 'rm -f "$snap"' EXIT
(cd "$root" && git ls-files -z -co --exclude-standard | tar --null -T - -cf "$snap")

case $1 in
fedora)
	# shellcheck disable=SC2016 # the text is for the shell inside the container
	script='
dnf -y -q install gcc make pam-devel authselect tar findutils diffutils >/dev/null
mkdir /work && tar -xf /src.tar -C /work && cd /work
make >/dev/null && make install PREFIX=/usr PAMDIR=/usr/lib64/security >/dev/null
# a known starting point, as on a freshly installed Fedora
authselect select local --force >/dev/null
authselect current --raw
cp /etc/pam.d/system-auth /tmp/system-auth.before
cp /etc/pam.d/fingerprint-auth /tmp/fingerprint-auth.before

echo "--- fpal setup --dry-run (must change nothing)"
fpal setup --dry-run --no-services
cmp /etc/pam.d/system-auth /tmp/system-auth.before

echo "--- fpal setup"
fpal setup --no-services
authselect current --raw
grep -n pam_fpal /etc/pam.d/system-auth /etc/pam.d/fingerprint-auth
if grep -q pam_fprintd /etc/pam.d/fingerprint-auth; then echo "pam_fprintd is still there"; exit 1; fi
grep -n "^auth" /etc/pam.d/system-auth | head -8

echo "--- fpal setup --revert"
fpal setup --revert --no-services
authselect current --raw
grep -c pam_fpal /etc/pam.d/system-auth /etc/pam.d/fingerprint-auth || true
grep -q pam_fpal /etc/pam.d/system-auth /etc/pam.d/fingerprint-auth && { echo "pam_fpal is still there"; exit 1; }
cmp /etc/pam.d/system-auth /tmp/system-auth.before
cmp /etc/pam.d/fingerprint-auth /tmp/fingerprint-auth.before
echo "--- fedora: setup and revert restore the PAM files exactly"
'
	;;
debian | ubuntu)
	# shellcheck disable=SC2016 # the text is for the shell inside the container
	script='
apt-get update -qq >/dev/null
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq gcc make libpam0g-dev libpam-runtime ca-certificates >/dev/null
mkdir /work && tar -xf /src.tar -C /work && cd /work
multiarch=$(gcc -dumpmachine)
make >/dev/null && make install PREFIX=/usr PAMDIR=/usr/lib/$multiarch/security \
	PAMCONFIGDIR=/usr/share/pam-configs >/dev/null
cp /etc/pam.d/common-auth /tmp/common-auth.before
grep -n "^auth" /etc/pam.d/common-auth

echo "--- fpal setup --dry-run (must change nothing)"
fpal setup --dry-run --no-services
cmp /etc/pam.d/common-auth /tmp/common-auth.before

echo "--- fpal setup"
fpal setup --no-services
grep -n "^auth" /etc/pam.d/common-auth
grep -q pam_fpal.so /etc/pam.d/common-auth || { echo "pam_fpal is not in common-auth"; exit 1; }
# the finger is tried first, the password stays behind it
fpal_line=$(grep -n pam_fpal.so /etc/pam.d/common-auth | head -n1 | cut -d: -f1)
unix_line=$(grep -n pam_unix.so /etc/pam.d/common-auth | head -n1 | cut -d: -f1)
[ "$fpal_line" -lt "$unix_line" ] || { echo "pam_fpal is not before pam_unix"; exit 1; }

echo "--- running it a second time changes nothing more"
cp /etc/pam.d/common-auth /tmp/common-auth.once
fpal setup --no-services
cmp /etc/pam.d/common-auth /tmp/common-auth.once

echo "--- fpal setup --revert"
fpal setup --revert --no-services
grep -q pam_fpal /etc/pam.d/common-auth && { echo "pam_fpal is still in common-auth"; exit 1; }
cmp /etc/pam.d/common-auth /tmp/common-auth.before
echo "--- '"$1"': setup and revert restore common-auth exactly"
'
	;;
esac

echo "=== $1 ($image)"
"$engine" run --rm --security-opt label=disable -v "$snap:/src.tar:ro" "$image" \
	sh -eu -c "$script"
