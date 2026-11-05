#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Login code (pam_fpal.so, fpald) must not be able to write to the sensor.
# Each driver lists what only its write support defines, next to its sources:
#
#   src/drivers/<name>/readonly.symbols   function and data symbols
#   src/drivers/<name>/readonly.patterns  byte patterns of the commands
#
# Symbols are the reliable check: they are looked up with nm in the static
# libraries and do not depend on how the compiler lays out constants.
#
#   sh scripts/check-readonly.sh --symbols build/libfpal-ro.a   none defined
#   sh scripts/check-readonly.sh --symbols --present build/libfpal.a
#                                                               all defined
#                                                               (positive control)
#
# Byte patterns are a best effort extra on the finished binaries: a compiler
# may fold short constants into immediate operands, so a pattern can be absent
# from a binary that does contain the command. They are only ever used to find
# commands, never to prove their absence on their own.
#
#   sh scripts/check-readonly.sh build/pam_fpal.so build/fpald
set -eu

export LC_ALL=C
root=$(cd "$(dirname "$0")/.." && pwd)
mode=bytes
present=no
bad=0

while [ "$#" -gt 0 ]; do
	case $1 in
	--symbols) mode=symbols ;;
	--present) present=yes ;;
	--) shift; break ;;
	-*) echo "usage: $0 [--symbols [--present]] FILE..." >&2; exit 2 ;;
	*) break ;;
	esac
	shift
done
[ "$#" -gt 0 ] || { echo "usage: $0 [--symbols [--present]] FILE..." >&2; exit 2; }

if [ "$mode" = symbols ]; then
	files=$(ls "$root"/src/drivers/*/readonly.symbols 2>/dev/null || true)
	[ -n "$files" ] || { echo "check-readonly: no readonly.symbols found" >&2; exit 2; }
	command -v nm >/dev/null || { echo "check-readonly: nm not found" >&2; exit 2; }

	for f in "$@"; do
		[ -f "$f" ] || { echo "check-readonly: $f not found" >&2; exit 2; }
		defined=$(nm -g --defined-only "$f" 2>/dev/null | awk 'NF >= 3 { print $3 }')
		for sf in $files; do
			driver=$(basename "$(dirname "$sf")")
			while IFS= read -r sym; do
				case $sym in '' | '#'*) continue ;; esac
				if printf '%s\n' "$defined" | grep -qx "$sym"; then
					found=yes
				else
					found=no
				fi
				if [ "$present" = no ] && [ "$found" = yes ]; then
					echo "check-readonly: $f defines $driver write symbol '$sym'" >&2
					bad=1
				elif [ "$present" = yes ] && [ "$found" = no ]; then
					echo "check-readonly: $f lacks $driver write symbol '$sym'" \
						"(stale symbol list?)" >&2
					bad=1
				fi
			done <"$sf"
		done
	done
	if [ "$bad" = 0 ]; then
		if [ "$present" = yes ]; then
			echo "check-readonly: $* defines every write symbol (positive control ok)"
		else
			echo "check-readonly: $* defines no write symbols"
		fi
	fi
	exit "$bad"
fi

# byte patterns
files=$(ls "$root"/src/drivers/*/readonly.patterns 2>/dev/null || true)
[ -n "$files" ] || { echo "check-readonly: no readonly.patterns found" >&2; exit 2; }
for f in "$@"; do
	[ -f "$f" ] || { echo "check-readonly: $f not found" >&2; exit 2; }
	for pf in $files; do
		driver=$(basename "$(dirname "$pf")")
		while IFS='|' read -r name pattern; do
			case $name in '' | '#'*) continue ;; esac
			if grep -qaP "$pattern" "$f"; then
				echo "check-readonly: $f contains the $driver '$name' command" >&2
				bad=1
			fi
		done <"$pf"
	done
done
[ "$bad" = 0 ] && echo "check-readonly: $* has no write byte patterns"
exit "$bad"
