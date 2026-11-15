#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Collect what FPAL needs to know about a fingerprint sensor it does not
# support yet, into one text file that the owner reads and then sends.
#
#   sudo sh sensor-info.sh            (works without sudo too, with less detail)
#
# It only reads: nothing is sent to the sensor, nothing is installed or
# changed. It leaves out serial numbers, UUIDs, MAC addresses, user and host
# names, and never touches the Windows key cache (Goodix_Cache.bin).
# For the ACPI part it needs the acpica-tools package (acpidump, iasl); without
# it that part is skipped and the file says how to add it.
set -u

VERSION=1
out="fpal-sensor-info-$(date +%Y%m%d-%H%M%S).txt"
root=0
[ "$(id -u)" = 0 ] && root=1

have() {
	command -v "$1" >/dev/null 2>&1
}

section() {
	printf '\n===== %s =====\n' "$1"
}

# Print a sysfs attribute, or "-" when it is missing.
attr() {
	if [ -r "$1" ]; then
		tr -d '\n' <"$1"
	else
		printf '%s' -
	fi
}

needs_root() {
	[ "$root" = 1 ] && return 0
	echo "(skipped: run with sudo to include this)"
	return 1
}

# The whole Device (...) { ... } block around a line of an ACPI .dsl file.
device_block() {
	file=$1 line=$2
	start=$(head -n "$line" "$file" | grep -n 'Device (' | tail -n 1 | cut -d: -f1)
	[ -n "$start" ] || start=$line
	awk -v s="$start" 'NR < s { next }
		{ print; n += gsub(/\{/, "{"); n -= gsub(/\}/, "}") }
		NR > s && n <= 0 && seen { exit }
		/\{/ { seen = 1 }' "$file"
}

collect() {
	echo "fpal sensor-info $VERSION, $(date -u +%Y-%m-%dT%H:%MZ)"
	echo "read-only: nothing was sent to the sensor; no serials, no names, no keys"
	[ "$root" = 1 ] || echo "note: not run as root, some parts are skipped"

	section "machine"
	for f in sys_vendor product_name product_version product_family \
		board_vendor board_name bios_vendor bios_version bios_date; do
		printf '%-16s %s\n' "$f:" "$(attr /sys/class/dmi/id/$f)"
	done

	section "system"
	echo "kernel:          $(uname -r) ($(uname -m))"
	if [ -r /etc/os-release ]; then
		# shellcheck disable=SC1091 # a file of the running system
		. /etc/os-release
		echo "distribution:    ${PRETTY_NAME:-unknown}"
	fi
	if have mokutil; then
		echo "secure boot:     $(mokutil --sb-state 2>&1 | head -n 1)"
	else
		echo "secure boot:     (mokutil not installed)"
	fi
	echo "lockdown:        $(attr /sys/kernel/security/lockdown)"
	if have efibootmgr; then
		if efibootmgr 2>/dev/null | grep -q "Windows Boot Manager"; then
			echo "windows:         a Windows Boot Manager entry exists (dual boot)"
		else
			echo "windows:         no Windows Boot Manager entry"
		fi
	fi

	section "usb devices (vendor:product, name; no serials)"
	for d in /sys/bus/usb/devices/*; do
		[ -r "$d/idVendor" ] || continue
		printf '%s:%s  %s  %s\n' "$(attr "$d/idVendor")" "$(attr "$d/idProduct")" \
			"$(attr "$d/manufacturer")" "$(attr "$d/product")"
	done | sort -u

	section "acpi devices that may be fingerprint sensors"
	found=0
	for d in /sys/bus/acpi/devices/*; do
		name=${d##*/}
		case "$name" in
		GXFP* | ELAN70* | FPC* | GDIX* | FTE* | ETU*) ;;
		*) continue ;;
		esac
		found=1
		echo "-- $name"
		echo "   hid:     $(attr "$d/hid")"
		echo "   path:    $(attr "$d/path")"
		echo "   status:  $(attr "$d/status")"
		echo "   modalias: $(attr "$d/modalias")"
		drv=$(readlink "$d/physical_node/driver" 2>/dev/null || readlink "$d/driver" 2>/dev/null)
		echo "   driver:  ${drv##*/}"
		phys=$(readlink -f "$d/physical_node" 2>/dev/null)
		echo "   node:    ${phys:--}"
	done
	[ "$found" = 1 ] || echo "(none)"

	section "spi, i2c and platform devices with a sensor-like name"
	found=0
	for d in /sys/bus/spi/devices/* /sys/bus/i2c/devices/* /sys/bus/platform/devices/*; do
		case "${d##*/}" in
		*[Gg][Xx][Ff][Pp]* | *[Gg][Oo][Oo][Dd][Ii][Xx]* | *[Ee][Ll][Aa][Nn]70* | \
			*[Ff][Pp][Cc]* | *[Ff][Ii][Nn][Gg][Ee][Rr]*)
			echo "${d#/sys/bus/}"
			found=1
			;;
		esac
	done
	[ "$found" = 1 ] || echo "(none)"

	section "kernel modules and device nodes"
	lsmod 2>/dev/null | grep -iE "^gxfp|goodix" || echo "(no gxfp or goodix module loaded)"
	ls -l /dev/gxfp* 2>/dev/null || echo "/dev/gxfp: missing"
	echo "gpio controllers (chip -> device):"
	for c in /sys/bus/gpio/devices/gpiochip*; do
		[ -e "$c" ] || continue
		echo "   ${c##*/} -> $(readlink -f "$c/.." 2>/dev/null | sed 's|.*/||')"
	done

	section "memory map lines of the sensor"
	if needs_root; then
		grep -i -B2 -A2 "gxfp" /proc/iomem || echo "(no line mentions gxfp)"
	fi

	section "kernel messages about the sensor"
	if needs_root; then
		dmesg 2>/dev/null | grep -iE "gxfp|goodix|espi|lockdown|fingerprint" | tail -n 60 ||
			echo "(none)"
	fi

	section "acpi description of the sensor"
	if ! have acpidump || ! have iasl; then
		echo "(skipped: install the acpica-tools package and run again:"
		echo "   sudo apt install acpica-tools   or   sudo dnf install acpica-tools)"
	elif needs_root; then
		tmp=$(mktemp -d) || return
		(
			cd "$tmp" || exit 1
			acpidump -b >/dev/null 2>&1
			for t in dsdt.dat ssdt*.dat; do
				[ -e "$t" ] && iasl -d "$t" >/dev/null 2>&1
			done
			hit=0
			for f in *.dsl; do
				[ -e "$f" ] || continue
				grep -n '_HID, "GXFP\|_HID, "ELAN70\|_HID, "FPC' "$f" |
					cut -d: -f1 | while read -r line; do
					echo "-- $f, line $line"
					device_block "$f" "$line"
				done
				if grep -q '_HID, "GXFP\|_HID, "ELAN70\|_HID, "FPC' "$f"; then
					hit=1
				fi
			done
			[ "$hit" = 1 ] || echo "(no sensor device found in the ACPI tables)"
			echo
			echo "-- other lines that mention GXFP (first 80)"
			grep -n "GXFP" ./*.dsl 2>/dev/null | head -n 80 || true
		)
		rm -rf "$tmp"
	fi

	section "end"
	echo "Please read this file before sending it."
}

collect >"$out" 2>&1
if [ -n "${SUDO_USER:-}" ]; then
	chown "$SUDO_USER" "$out" 2>/dev/null
fi
echo "written: $PWD/$out"
echo "Please read it, then send it to the FPAL developer (paste or attach)."
