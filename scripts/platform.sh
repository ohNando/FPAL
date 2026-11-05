#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Switch an authselect based system (Fedora, RHEL) from pam_fprintd to
# pam_fpal, or back.
#
#   sudo scripts/authselect-fpal.sh install   # custom/fpal profile, mask fprintd,
#                                             # start fpald for GNOME
#   sudo scripts/authselect-fpal.sh revert    # back to the stock profile
#
# Only the pam_fprintd lines change; the password stays a fallback.
set -eu

PROFILE=fpal
BASE=${BASE:-local}
DIR=/etc/authselect/custom/$PROFILE

die() {
	echo "authselect-fpal: $*" >&2
	exit 1
}

[ "$(id -u)" = 0 ] || die "run as root"
command -v authselect >/dev/null || die "authselect not found"

# "local with-a with-b" -> "with-a with-b"
features() {
	authselect current --raw | cut -s -d' ' -f2-
}

install_profile() {
	found=
	for d in /usr/lib64/security /usr/lib/security /lib/security; do
		if [ -e "$d/pam_fpal.so" ]; then
			found=$d
		fi
	done
	[ -n "$found" ] || die "pam_fpal.so is not installed (sudo make install)"

	if [ ! -d "$DIR" ]; then
		authselect create-profile "$PROFILE" -b "$BASE" --symlink-meta
	fi
	sed -i 's/pam_fprintd\.so/pam_fpal.so/' "$DIR/system-auth" \
		"$DIR/fingerprint-auth"
	# The stock hint asks to enable fprintd, which is the opposite of us.
	sed -i 's/make sure fprintd service is configured and enabled/make sure pam_fpal.so is installed and fprintd is masked/' \
		"$DIR/REQUIREMENTS"
	grep -q pam_fpal.so "$DIR/system-auth" ||
		die "$DIR/system-auth has no fingerprint line"

	feats=$(features)
	case " $feats " in
	*" with-fingerprint "*) ;;
	*) feats="$feats with-fingerprint" ;;
	esac

	# shellcheck disable=SC2086 # feature list is split on purpose
	authselect select "custom/$PROFILE" $feats -b
	authselect apply-changes

	# fprintd would grab the sensor before pam_fpal can open it.
	systemctl stop fprintd.service 2>/dev/null || true
	systemctl mask fprintd.service

	# GNOME asks net.reactivated.Fprint whether a reader exists before it
	# offers fingerprint on the login and lock screens; fpald answers.
	if systemctl cat fpald.service >/dev/null 2>&1; then
		# the bus must read fpald's policy file before the name is owned
		systemctl reload dbus.service 2>/dev/null || true
		systemctl enable --now fpald.service
	else
		echo "authselect-fpal: fpald.service not installed, GNOME will not" \
			"offer fingerprint (sudo make install-fpald)" >&2
	fi

	echo "authselect-fpal: custom/$PROFILE active ($feats)"
}

revert_profile() {
	feats=$(features)
	# shellcheck disable=SC2086
	authselect select "$BASE" $feats -b
	# fprintd takes the bus name back
	systemctl disable --now fpald.service 2>/dev/null || true
	systemctl unmask fprintd.service
	echo "authselect-fpal: $BASE active again, fprintd unmasked, fpald stopped"
}

case "${1:-}" in
install) install_profile ;;
revert) revert_profile ;;
*) die "usage: $0 install|revert" ;;
esac
