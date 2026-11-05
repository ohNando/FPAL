#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Make a system use pam_fpal, or go back. "fpal setup" runs this as root.
#
#   platform.sh BACKEND ACTION [--dry-run] [--no-services]
#
#   BACKEND  authselect       Fedora, RHEL: custom/fpal profile
#            pam-auth-update  Debian, Ubuntu: the "fpal" PAM profile
#            manual           change nothing, print what to do by hand
#   ACTION   install | revert | status
#
#   --dry-run      say what would be done, change nothing
#   --no-services  leave fprintd and fpald alone (containers, tests)
#
# Only the fingerprint line changes; the password stays a fallback.
#
# FPAL_PLATFORM_ROOT (tests only) is put in front of every system path and
# switches the root check off; real commands are never reached that way,
# the tests put fakes first in PATH.
set -eu

PROFILE=fpal
BASE=${BASE:-local}
ROOT=${FPAL_PLATFORM_ROOT:-}
DRY_RUN=0
SERVICES=1
export DEBIAN_FRONTEND=noninteractive

die() {
	echo "fpal setup: $*" >&2
	exit 1
}

say() {
	echo "fpal setup: $*"
}

usage() {
	echo "usage: $0 [authselect|pam-auth-update|manual] install|revert|status" \
		"[--dry-run] [--no-services]" >&2
	exit 2
}

have() {
	command -v "$1" >/dev/null 2>&1
}

# run CMD...: do it, or only say so with --dry-run
run() {
	if [ "$DRY_RUN" = 1 ]; then
		echo "  would run: $*"
	else
		"$@"
	fi
}

# try CMD...: like run, but a failure does not matter
try() {
	if [ "$DRY_RUN" = 1 ]; then
		echo "  would run: $*"
	else
		"$@" >/dev/null 2>&1 || true
	fi
}

need_root() {
	[ -n "$ROOT" ] || [ "$DRY_RUN" = 1 ] || [ "$(id -u)" = 0 ] ||
		die "run as root (sudo fpal setup)"
}

# Print the directory that holds pam_fpal.so.
find_module() {
	for d in "$ROOT"/usr/lib64/security "$ROOT"/usr/lib/security \
		"$ROOT"/lib/security "$ROOT"/usr/lib/*/security \
		"$ROOT"/lib/*/security; do
		if [ -e "$d/pam_fpal.so" ]; then
			echo "$d"
			return 0
		fi
	done
	return 1
}

check_module() {
	if dir=$(find_module); then
		say "pam_fpal.so found in $dir"
	elif [ "$DRY_RUN" = 1 ]; then
		say "pam_fpal.so is NOT installed; a real run would stop here"
	else
		die "pam_fpal.so is not installed (sudo make install)"
	fi
}

# ---- services --------------------------------------------------------------

services_install() {
	if [ "$SERVICES" = 0 ]; then
		say "leaving fprintd and fpald alone (--no-services)"
		return 0
	fi
	have systemctl || {
		say "systemctl not found, leaving fprintd and fpald alone"
		return 0
	}
	# fprintd would grab the sensor before pam_fpal can open it.
	try systemctl stop fprintd.service
	run systemctl mask fprintd.service

	# GNOME asks net.reactivated.Fprint whether a reader exists before it
	# offers fingerprint on the login and lock screens; fpald answers.
	if systemctl cat fpald.service >/dev/null 2>&1; then
		# the bus must read fpald's policy file before the name is owned
		try systemctl reload dbus.service
		run systemctl enable --now fpald.service
	else
		echo "fpal setup: fpald.service not installed, GNOME will not" \
			"offer fingerprint (sudo make install-fpald)" >&2
	fi
}

services_revert() {
	if [ "$SERVICES" = 0 ]; then
		say "leaving fprintd and fpald alone (--no-services)"
		return 0
	fi
	have systemctl || return 0
	# fprintd takes the bus name back
	try systemctl disable --now fpald.service
	run systemctl unmask fprintd.service
}

services_status() {
	have systemctl || return 0
	echo "fprintd:  $(systemctl is-enabled fprintd.service 2>/dev/null || true)"
	echo "fpald:    $(systemctl is-active fpald.service 2>/dev/null || true)"
}

# ---- authselect (Fedora, RHEL) -----------------------------------------------

AS_DIR=$ROOT/etc/authselect/custom/$PROFILE
# Present while with-fingerprint was added by "fpal setup" (it was not selected
# before), so that --revert can take it out again.
AS_ADDED=$ROOT/var/lib/fpal/.authselect-added-fingerprint

# "local with-a with-b" -> "with-a with-b"
as_features() {
	authselect current --raw 2>/dev/null | cut -s -d' ' -f2- || true
}

as_install() {
	need_root
	have authselect || die "authselect not found"
	check_module

	if [ ! -d "$AS_DIR" ]; then
		run authselect create-profile "$PROFILE" -b "$BASE" --symlink-meta
	fi
	if [ "$DRY_RUN" = 1 ]; then
		echo "  would point $AS_DIR/system-auth and fingerprint-auth at pam_fpal.so"
	else
		sed -i 's/pam_fprintd\.so/pam_fpal.so/' "$AS_DIR/system-auth" \
			"$AS_DIR/fingerprint-auth"
		# The stock hint asks to enable fprintd, which is the opposite of us.
		sed -i 's/make sure fprintd service is configured and enabled/make sure pam_fpal.so is installed and fprintd is masked/' \
			"$AS_DIR/REQUIREMENTS"
		grep -q pam_fpal.so "$AS_DIR/system-auth" ||
			die "$AS_DIR/system-auth has no fingerprint line"
	fi

	feats=$(as_features)
	added=0
	case " $feats " in
	*" with-fingerprint "*) ;;
	*)
		feats="$feats with-fingerprint"
		added=1
		;;
	esac

	# shellcheck disable=SC2086 # feature list is split on purpose
	run authselect select "custom/$PROFILE" $feats -b
	run authselect apply-changes
	if [ "$added" = 1 ] && [ "$DRY_RUN" = 0 ]; then
		mkdir -p "$(dirname "$AS_ADDED")"
		: >"$AS_ADDED"
	fi
	services_install
	[ "$DRY_RUN" = 1 ] || say "custom/$PROFILE active ($feats)"
}

as_revert() {
	need_root
	have authselect || die "authselect not found"
	feats=$(as_features)
	if [ -e "$AS_ADDED" ]; then
		# we added with-fingerprint; leave the system as it was
		feats=$(echo " $feats " | sed 's/ with-fingerprint / /g')
	fi
	# shellcheck disable=SC2086
	run authselect select "$BASE" $feats -b
	if [ "$DRY_RUN" = 0 ]; then
		rm -f "$AS_ADDED"
	fi
	services_revert
	[ "$DRY_RUN" = 1 ] || say "$BASE active again, fprintd unmasked, fpald stopped"
}

as_status() {
	have authselect || die "authselect not found"
	cur=$(authselect current --raw 2>/dev/null | head -n1 || true)
	echo "profile:  ${cur:-none}"
	case "$cur" in
	custom/$PROFILE*) echo "fpal:     active" ;;
	*) echo "fpal:     not active" ;;
	esac
	services_status
}

# ---- pam-auth-update (Debian, Ubuntu) ------------------------------------------

PAM_CONFIG=$ROOT/usr/share/pam-configs/fpal

# The profile file as shipped: next to this script, or in a source checkout.
shipped_pam_config() {
	here=$(dirname "$0")
	for f in "$here/pam-configs/fpal" "$here/../data/pam-configs/fpal"; do
		if [ -f "$f" ]; then
			echo "$f"
			return 0
		fi
	done
	return 1
}

pau_install() {
	need_root
	have pam-auth-update || die "pam-auth-update not found"
	check_module

	if [ ! -e "$PAM_CONFIG" ]; then
		src=$(shipped_pam_config) ||
			die "the PAM profile file pam-configs/fpal was not found next to this script"
		run install -D -m 0644 "$src" "$PAM_CONFIG"
	fi
	run pam-auth-update --enable "$PROFILE"
	services_install
	[ "$DRY_RUN" = 1 ] || say "the fpal PAM profile is enabled; undo with: fpal setup --revert"
}

pau_revert() {
	need_root
	have pam-auth-update || die "pam-auth-update not found"
	run pam-auth-update --remove "$PROFILE"
	services_revert
	[ "$DRY_RUN" = 1 ] || say "the fpal PAM profile is removed, fprintd unmasked, fpald stopped"
}

pau_status() {
	if grep -qs 'pam_fpal\.so' "$ROOT/etc/pam.d/common-auth"; then
		echo "fpal:     in /etc/pam.d/common-auth"
	else
		echo "fpal:     not in /etc/pam.d/common-auth"
	fi
	services_status
}

# ---- manual ----------------------------------------------------------------

manual() {
	case "$1" in
	install)
		cat <<'TXT'
Nothing is changed. To use the fingerprint by hand:

 1. In every PAM service that should accept a finger (for example
    /etc/pam.d/sudo and /etc/pam.d/su, and the fingerprint stack of your
    login manager) put this line first in the auth section:

        auth    sufficient    pam_fpal.so

    It is "sufficient": when the finger does not match, the password
    prompt still comes.

 2. fprintd would take the sensor before pam_fpal can open it:

        systemctl mask fprintd.service

 3. For the GNOME login and lock screens, start the small service that
    tells GNOME there is a reader:

        systemctl reload dbus.service
        systemctl enable --now fpald.service
TXT
		;;
	revert)
		cat <<'TXT'
Nothing is changed. To go back by hand:

 1. Remove the "pam_fpal.so" line from the PAM files you edited
    (find them with: grep -l pam_fpal.so /etc/pam.d/*).
 2. systemctl unmask fprintd.service
 3. systemctl disable --now fpald.service
TXT
		;;
	status)
		echo "backend:  manual (fpal does not manage the PAM files on this system)"
		echo "files:"
		grep -ls 'pam_fpal\.so' "$ROOT"/etc/pam.d/* 2>/dev/null | sed 's/^/  /' || true
		services_status
		;;
	esac
}

# ---- main -------------------------------------------------------------------

backend=
action=
for arg in "$@"; do
	case "$arg" in
	--dry-run) DRY_RUN=1 ;;
	--no-services) SERVICES=0 ;;
	authselect | pam-auth-update | manual) backend=$arg ;;
	install | revert | status) action=$arg ;;
	*) usage ;;
	esac
done
[ -n "$action" ] || usage
# Old invocations ("authselect-fpal.sh install") name no backend.
[ -n "$backend" ] || backend=authselect

if [ "$DRY_RUN" = 1 ]; then
	say "dry run: nothing will be changed"
fi

case "$backend" in
manual) manual "$action" ;;
authselect) "as_$action" ;;
pam-auth-update) "pau_$action" ;;
esac

if [ "$DRY_RUN" = 1 ]; then
	say "dry run finished: nothing was changed"
fi
