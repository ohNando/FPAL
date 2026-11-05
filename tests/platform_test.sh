#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# scripts/platform.sh against fake authselect, pam-auth-update and systemctl.
# The fakes only record what they were asked; every system path is moved under
# a scratch directory (FPAL_PLATFORM_ROOT), so nothing here touches the real
# machine.
#
#   sh tests/platform_test.sh scripts/platform.sh
set -u

SCRIPT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SHIM=$(dirname "$SCRIPT")/authselect-fpal.sh
SH=$(command -v sh)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
FAILED=0
N=0

pass() { echo "  ok   $1"; }
fail() { echo "  FAIL $1"; FAILED=$((FAILED + 1)); }
check() {
	desc=$1
	shift
	N=$((N + 1))
	if "$@"; then pass "$desc"; else fail "$desc"; fi
}
logged() { grep -q -- "$1" "$LOG"; }
not_logged() { ! grep -q -- "$1" "$LOG"; }
has() { grep -q -- "$1" "$2"; }
hasnt() { ! grep -q -- "$1" "$2"; }

# A fresh world: ROOT with the module installed, BIN with the tools the script
# needs plus the fakes named in $1.
new_world() {
	W=$T/w$N$$
	ROOT=$W/root
	BIN=$W/bin
	LOG=$W/log
	mkdir -p "$ROOT/usr/lib64/security" "$ROOT/etc/pam.d" "$BIN"
	: >"$ROOT/usr/lib64/security/pam_fpal.so"
	: >"$LOG"
	for tool in sed grep cut head dirname install id cat mkdir; do
		ln -s "$(command -v $tool)" "$BIN/$tool"
	done
	for fake in $1; do "fake_$fake"; done
}

fake_authselect() {
	cat >"$BIN/authselect" <<FAKE
#!/bin/sh
echo "authselect \$*" >>"$LOG"
case "\$1" in
create-profile)
	d=$ROOT/etc/authselect/custom/\$2
	mkdir -p "\$d"
	printf 'auth sufficient pam_fprintd.so\n' >"\$d/system-auth"
	printf 'auth sufficient pam_fprintd.so\n' >"\$d/fingerprint-auth"
	printf 'make sure fprintd service is configured and enabled\n' >"\$d/REQUIREMENTS"
	;;
current)
	[ -f "$W/current" ] && cat "$W/current" || echo "sssd with-mkhomedir"
	;;
esac
FAKE
	chmod +x "$BIN/authselect"
}

fake_systemctl() {
	cat >"$BIN/systemctl" <<FAKE
#!/bin/sh
echo "systemctl \$*" >>"$LOG"
case "\$1" in
cat) exit 0 ;;
is-enabled) echo masked ;;
is-active) echo active ;;
esac
FAKE
	chmod +x "$BIN/systemctl"
}

fake_pam_auth_update() {
	cat >"$BIN/pam-auth-update" <<FAKE
#!/bin/sh
echo "pam-auth-update \$*" >>"$LOG"
f=$ROOT/etc/pam.d/common-auth
case "\$1" in
--enable) echo 'auth [success=end default=ignore] pam_fpal.so' >>"\$f" ;;
--remove) [ -f "\$f" ] && sed -i '/pam_fpal/d' "\$f" ;;
esac
FAKE
	chmod +x "$BIN/pam-auth-update"
}

# Run the script in the current world; output in $W/out, status in $RC.
play() {
	RC=0
	PATH=$BIN FPAL_PLATFORM_ROOT=$ROOT "$SH" "$@" >"$W/out" 2>&1 || RC=$?
}
script() { play "$SCRIPT" "$@"; }

echo "== manual: prints, changes nothing"
for action in install revert status; do
	new_world "authselect systemctl pam_auth_update"
	script manual $action
	check "manual $action succeeds" test "$RC" = 0
	check "manual $action changed nothing" not_logged 'systemctl \(mask\|unmask\|enable \|disable\|stop\|reload\)\|authselect \(select\|create\|apply\)\|pam-auth-update'
	check "manual $action touched no file" test ! -e "$ROOT/etc/authselect"
done
new_world ""
script manual install
check "manual install shows the PAM line" has 'auth    sufficient    pam_fpal.so' "$W/out"
check "manual needs no tools at all" test "$RC" = 0

echo "== authselect"
new_world "authselect systemctl"
script authselect install --dry-run
check "dry run succeeds" test "$RC" = 0
check "dry run says what it would do" has 'would run: authselect select custom/fpal' "$W/out"
check "dry run creates no profile" test ! -d "$ROOT/etc/authselect"
check "dry run changes nothing" not_logged 'authselect \(create-profile\|select\|apply-changes\)\|systemctl \(mask\|enable\|stop\|reload\)'

new_world "authselect systemctl"
script authselect install
check "install succeeds" test "$RC" = 0
check "install creates the profile" logged 'create-profile fpal -b local --symlink-meta'
check "install points the fingerprint line at pam_fpal" has pam_fpal.so "$ROOT/etc/authselect/custom/fpal/system-auth"
check "install leaves no pam_fprintd" hasnt pam_fprintd "$ROOT/etc/authselect/custom/fpal/fingerprint-auth"
check "install fixes the requirement hint" has 'pam_fpal.so is installed and fprintd is masked' "$ROOT/etc/authselect/custom/fpal/REQUIREMENTS"
check "install selects the profile with fingerprint" logged 'select custom/fpal with-mkhomedir with-fingerprint -b'
check "install applies" logged 'apply-changes'
check "install masks fprintd" logged 'systemctl mask fprintd.service'
check "install starts fpald" logged 'systemctl enable --now fpald.service'

new_world "authselect systemctl"
script authselect install --no-services
check "no-services install succeeds" test "$RC" = 0
check "no-services touches no service" not_logged 'systemctl'
check "no-services still selects the profile" logged 'select custom/fpal'

new_world "authselect systemctl"
echo "custom/fpal with-fingerprint with-mkhomedir" >"$W/current"
script authselect install
check "an already selected feature is not added twice" logged 'select custom/fpal with-fingerprint with-mkhomedir -b'

new_world "authselect systemctl"
script authselect revert
check "revert succeeds" test "$RC" = 0
check "revert selects the stock profile" logged 'select local with-mkhomedir -b'
check "revert unmasks fprintd" logged 'systemctl unmask fprintd.service'
check "revert stops fpald" logged 'systemctl disable --now fpald.service'

new_world "authselect systemctl"
rm "$ROOT/usr/lib64/security/pam_fpal.so"
script authselect install
check "install without the module fails" test "$RC" != 0
check "... and says why" has 'pam_fpal.so is not installed' "$W/out"
check "... before changing anything" not_logged 'authselect \(create-profile\|select\)'

new_world "authselect systemctl"
rm "$ROOT/usr/lib64/security/pam_fpal.so"
mkdir -p "$ROOT/usr/lib/x86_64-linux-gnu/security"
: >"$ROOT/usr/lib/x86_64-linux-gnu/security/pam_fpal.so"
script authselect install --dry-run
check "the module is found in a Debian style directory" has 'found in .*x86_64-linux-gnu/security' "$W/out"

new_world "systemctl"
script authselect install
check "no authselect: fails with a message" test "$RC" != 0
check "... naming it" has 'authselect not found' "$W/out"

new_world "authselect systemctl"
script authselect status
check "status succeeds" test "$RC" = 0
check "status says fpal is not active" has 'not active' "$W/out"

echo "== pam-auth-update"
new_world "pam_auth_update systemctl"
script pam-auth-update install --dry-run
check "dry run succeeds" test "$RC" = 0
check "dry run says what it would do" has 'would run: pam-auth-update --enable fpal' "$W/out"
check "dry run installs no profile file" test ! -e "$ROOT/usr/share/pam-configs/fpal"
check "dry run runs no pam-auth-update" not_logged 'pam-auth-update'
check "dry run changes no service" not_logged 'systemctl \(mask\|enable\|stop\|reload\)'

new_world "pam_auth_update systemctl"
script pam-auth-update install
check "install succeeds" test "$RC" = 0
check "install puts the profile in place" has 'pam_fpal.so' "$ROOT/usr/share/pam-configs/fpal"
check "install enables it" logged 'pam-auth-update --enable fpal'
check "install puts pam_fpal into common-auth" has pam_fpal.so "$ROOT/etc/pam.d/common-auth"
check "install masks fprintd" logged 'systemctl mask fprintd.service'

new_world "pam_auth_update systemctl"
mkdir -p "$ROOT/usr/share/pam-configs"
echo packaged >"$ROOT/usr/share/pam-configs/fpal"
script pam-auth-update install --no-services
check "an existing (packaged) profile is not overwritten" has packaged "$ROOT/usr/share/pam-configs/fpal"
check "no-services touches no service" not_logged 'systemctl'

new_world "pam_auth_update systemctl"
script pam-auth-update install --no-services
script pam-auth-update status
check "status sees the line" has 'in /etc/pam.d/common-auth' "$W/out"
script pam-auth-update revert
check "revert succeeds" test "$RC" = 0
check "revert removes the profile" logged 'pam-auth-update --remove fpal'
check "revert takes the line out" hasnt pam_fpal "$ROOT/etc/pam.d/common-auth"
check "revert unmasks fprintd" logged 'systemctl unmask fprintd.service'

new_world "systemctl"
script pam-auth-update install
check "no pam-auth-update: fails with a message" test "$RC" != 0
check "... naming it" has 'pam-auth-update not found' "$W/out"

echo "== root is required for a real run"
if [ "$(id -u)" != 0 ]; then
	new_world "authselect systemctl pam_auth_update"
	for backend in authselect pam-auth-update; do
		RC=0
		PATH=$BIN "$SH" "$SCRIPT" $backend install >"$W/out" 2>&1 || RC=$?
		check "$backend install as a normal user is refused" test "$RC" != 0
		check "... with a clear message" has 'run as root' "$W/out"
	done
	check "... and nothing was run" test ! -s "$LOG"
	RC=0
	PATH=$BIN "$SH" "$SCRIPT" authselect install --dry-run >"$W/out" 2>&1 || RC=$?
	check "a dry run needs no root" test "$RC" = 0
else
	echo "  (running as root: skipped)"
fi

echo "== arguments and the old name"
new_world ""
script
check "no arguments: usage, status 2" test "$RC" = 2
script frobnicate
check "unknown word: usage" test "$RC" = 2
script authselect
check "no action: usage" test "$RC" = 2
script authselect install --yes
check "unknown option: usage" test "$RC" = 2

new_world "authselect systemctl"
play "$SHIM" install --no-services
check "the old authselect-fpal.sh name still works" logged 'select custom/fpal'
new_world "authselect systemctl"
play "$SHIM" revert --no-services
check "... also for revert" logged 'select local'

echo
if [ "$FAILED" = 0 ]; then
	echo "platform_test: $N checks, all passed"
else
	echo "platform_test: $FAILED of $N checks FAILED"
	exit 1
fi
