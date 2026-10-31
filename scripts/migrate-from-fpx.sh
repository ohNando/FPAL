#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# One-time move of a machine that ran the pre-release "fpx" build over to
# fpal: bindings, authselect profile, installed files.
#
#   sudo make install                       # installs pam_fpal.so first
#   sudo scripts/migrate-from-fpx.sh
#
# The password stays a fallback throughout; the old profile is only removed
# after the new one is confirmed active.
set -eu

die() {
	echo "migrate-from-fpx: $*" >&2
	exit 1
}

[ "$(id -u)" = 0 ] || die "run as root"

here=$(dirname "$0")
found=
for d in /usr/lib64/security /usr/lib/security /lib/security; do
	if [ -e "$d/pam_fpal.so" ]; then
		found=$d
	fi
done
[ -n "$found" ] || die "pam_fpal.so is not installed (sudo make install)"

# 1. Bindings: same files, new directory.
if [ -d /var/lib/fpx ] && [ ! -e /var/lib/fpal ]; then
	mv /var/lib/fpx /var/lib/fpal
	echo "moved /var/lib/fpx -> /var/lib/fpal"
fi

# 2. Switch the PAM stack to the new profile.
"$here/authselect-fpal.sh" install
authselect current --raw | grep -q '^custom/fpal ' ||
	die "custom/fpal is not active, old files left in place"

# 3. Only now remove what the old build left behind.
if [ -d /etc/authselect/custom/fpx ]; then
	rm -rf /etc/authselect/custom/fpx
fi
rm -f "$found/pam_fpx.so" /usr/local/sbin/fpx /etc/pam.d/fpx-test /run/fpx.lock

echo "migrate-from-fpx: done"
