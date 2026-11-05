#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Compare what two builds of fpal send to the real sensor. Run it with the old
# and the new binary and diff the results; the commands the host sends (OUT and
# control transfers) must be the same. The traces contain your template ids:
# keep them on your machine, do not paste or commit them.
#
#   sudo scripts/capture-trace.sh capture /usr/bin/fpal       old.trace
#   sudo scripts/capture-trace.sh capture build/fpal          new.trace
#   scripts/capture-trace.sh diff old.trace new.trace
set -eu

usage() {
	echo "usage: $0 capture FPAL OUTFILE | $0 diff OLD NEW" >&2
	exit 2
}

# Keep what the host sends, without the timestamps and without the sensor's
# answers (an answer such as a match can legitimately differ between runs).
host_frames() {
	sed 's/^\[trace +[ 0-9.]* ms\]/[trace]/' "$1" |
		awk '/^\[trace\] (OUT|CTRL)/ { keep = 1; print; next }
		     /^\[trace\]/ { keep = 0; next }
		     keep { print }'
}

case "${1:-}" in
capture)
	[ "$#" = 3 ] || usage
	fpal=$2 out=$3
	[ "$(id -u)" = 0 ] || { echo "capture needs root (sudo)" >&2; exit 1; }
	: >"$out"
	for cmd in "info" "list --raw"; do
		echo "== fpal $cmd" >>"$out"
		# shellcheck disable=SC2086 # the command is split on purpose
		FPAL_TRACE=1 "$fpal" $cmd >/dev/null 2>>"$out" || true
	done
	echo "== fpal identify: touch the sensor within 15 seconds"
	echo "== fpal identify" >>"$out"
	FPAL_TRACE=1 "$fpal" identify -t 15000 >/dev/null 2>>"$out" || true
	echo "trace written to $out"
	;;
diff)
	[ "$#" = 3 ] || usage
	old=$(mktemp) new=$(mktemp)
	trap 'rm -f "$old" "$new"' EXIT
	host_frames "$2" >"$old"
	host_frames "$3" >"$new"
	if diff -u "$old" "$new"; then
		echo "same: both builds sent the same frames to the sensor"
	else
		echo "DIFFERENT: the frames sent to the sensor changed" >&2
		exit 1
	fi
	;;
*)
	usage
	;;
esac
