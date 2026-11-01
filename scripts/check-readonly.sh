#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Fail if a binary contains one of the Egis commands that write to the
# sensor: store a template, start a commit, start enrolling, delete.
#
#   sh scripts/check-readonly.sh build/pam_fpal.so build/fpald
set -eu

export LC_ALL=C
bad=0

# command bytes as grep -P escapes (see src/drivers/egis_moc/proto.c)
check() {
	file=$1 name=$2 pattern=$3
	if grep -qaP "$pattern" "$file"; then
		echo "check-readonly: $file contains the $name command" >&2
		bad=1
	fi
}

for f in "$@"; do
	[ -f "$f" ] || { echo "check-readonly: $f not found" >&2; exit 2; }
	check "$f" "store"        '\x00\x00\x00\x27\x50\x16\x03\x00\x00\x00\x20'
	check "$f" "commit start" '\x00\x00\x00\x07\x50\x16\x05\x00\x00\x00\x20'
	check "$f" "enrol start"  '\x00\x00\x00\x07\x50\x16\x01\x00\x00\x00\x20'
	check "$f" "delete"       '\x50\x18\x04\x00\x00'
done

[ "$bad" = 0 ] && echo "check-readonly: $* has no write commands"
exit "$bad"
