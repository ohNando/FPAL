#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Every driver lists the byte patterns of its commands that write to the
# sensor in src/drivers/<name>/readonly.patterns. Login code must not contain
# any of them:
#
#   sh scripts/check-readonly.sh build/pam_fpal.so build/fpald
#
# Positive control: with --present the same patterns MUST all be there (the
# full fpal), so a typo or a stale pattern file cannot make the check pass
# silently:
#
#   sh scripts/check-readonly.sh --present build/fpal
set -eu

export LC_ALL=C
root=$(cd "$(dirname "$0")/.." && pwd)
mode=absent
bad=0

if [ "${1:-}" = "--present" ]; then
	mode=present
	shift
fi
[ "$#" -gt 0 ] || { echo "usage: $0 [--present] FILE..." >&2; exit 2; }

files=$(ls "$root"/src/drivers/*/readonly.patterns 2>/dev/null || true)
[ -n "$files" ] || { echo "check-readonly: no readonly.patterns found" >&2; exit 2; }

for f in "$@"; do
	[ -f "$f" ] || { echo "check-readonly: $f not found" >&2; exit 2; }
	for pf in $files; do
		driver=$(basename "$(dirname "$pf")")
		while IFS='|' read -r name pattern; do
			case $name in '' | '#'*) continue ;; esac
			found=no
			grep -qaP "$pattern" "$f" && found=yes
			if [ "$mode" = absent ] && [ "$found" = yes ]; then
				echo "check-readonly: $f contains the $driver '$name' command" >&2
				bad=1
			elif [ "$mode" = present ] && [ "$found" = no ]; then
				echo "check-readonly: $f is missing the $driver '$name' pattern" \
					"(stale pattern file?)" >&2
				bad=1
			fi
		done <"$pf"
	done
done

if [ "$bad" = 0 ]; then
	if [ "$mode" = present ]; then
		echo "check-readonly: $* has every write pattern (positive control ok)"
	else
		echo "check-readonly: $* has no write commands"
	fi
fi
exit "$bad"
