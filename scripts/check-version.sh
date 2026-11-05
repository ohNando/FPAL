#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Fail if the version differs between the places that carry it: config.h, the
# RPM spec (Version and its newest %changelog entry), the Debian changelog,
# the Arch PKGBUILD and the newest released section of CHANGELOG.md.
#
#   sh scripts/check-version.sh
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

config=$(sed -n 's/^#define FPAL_VERSION "\(.*\)"/\1/p' include/fpal/config.h)
spec=$(sed -n 's/^Version:[[:space:]]*//p' packaging/fpal.spec | head -n 1)
spec_log=$(sed -n '/^%changelog/,$ s/^\* .* - \([0-9][^ ]*\)-[0-9]*$/\1/p' packaging/fpal.spec | head -n 1)
deb=$(sed -n '1s/^fpal (\([^-)]*\)-.*/\1/p' packaging/debian/changelog)
arch=$(sed -n 's/^pkgver=//p' packaging/arch/PKGBUILD)
changelog=$(sed -n 's/^## \[\([0-9][^]]*\)\].*/\1/p' CHANGELOG.md | head -n 1)

bad=0
printf '%-26s %s\n' "include/fpal/config.h" "$config"
printf '%-26s %s\n' "packaging/fpal.spec" "$spec"
printf '%-26s %s\n' "spec %changelog (newest)" "$spec_log"
printf '%-26s %s\n' "debian/changelog" "$deb"
printf '%-26s %s\n' "arch/PKGBUILD" "$arch"
printf '%-26s %s\n' "CHANGELOG.md (newest)" "$changelog"

for v in "$spec" "$spec_log" "$deb" "$arch" "$changelog"; do
	[ "$v" = "$config" ] || bad=1
done
[ -n "$config" ] || bad=1

if [ "$bad" = 0 ]; then
	echo "check-version: all places say $config"
else
	echo "check-version: the versions above differ" >&2
fi
exit "$bad"
