#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Build and test fpal inside a clean container of another distribution, the
# same steps the CI matrix runs, so a failure can be reproduced here:
#
#   scripts/ci-container.sh fedora|debian|ubuntu|arch|all
#
# Uses podman, or docker if there is no podman. The working tree is given
# as a tarball snapshot and unpacked inside; nothing on the host is changed.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
engine=${CONTAINER_ENGINE:-$(command -v podman || command -v docker || true)}
[ -n "$engine" ] || { echo "ci-container: neither podman nor docker found" >&2; exit 2; }

image_for() {
	case $1 in
	fedora) echo docker.io/library/fedora:latest ;;
	debian) echo docker.io/library/debian:stable ;;
	ubuntu) echo docker.io/library/ubuntu:24.04 ;;
	arch) echo docker.io/library/archlinux:latest ;;
	*) return 1 ;;
	esac
}

# Packages needed to build and run the tests, as one command per family.
deps_for() {
	case $1 in
	fedora) echo "dnf -y -q install gcc make pam-devel git tar findutils" ;;
	debian | ubuntu) echo "apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq gcc make libpam0g-dev git ca-certificates procps" ;;
	arch) echo "pacman -Syu --noconfirm --quiet gcc make pam git" ;;
	esac
}

# A snapshot of the working tree (tracked and new files, not ignored ones), so
# edits made while the containers run do not change what is being tested.
snapshot() {
	snap=$(mktemp)
	(cd "$root" && git ls-files -z -co --exclude-standard | tar --null -T - -cf "$snap")
}

run_one() {
	distro=$1
	image=$(image_for "$distro") || { echo "ci-container: unknown distro '$distro'" >&2; exit 2; }
	echo "=== $distro ($image)"
	"$engine" run --rm --security-opt label=disable \
		-v "$snap:/src.tar:ro" "$image" sh -eu -c "
		$(deps_for "$distro")
		git config --global --add safe.directory '*'
		mkdir /work && tar -xf /src.tar -C /work && cd /work
		gcc --version | head -n 1
		make
		make test
		make ENROLL=0 test
		make fuzz FUZZ_PERCENT=2
		build/fpal version
		echo '--- $distro: build and tests passed'
	"
}

snapshot
trap 'rm -f "$snap"' EXIT

case ${1:-} in
all)
	for d in fedora debian ubuntu arch; do run_one "$d"; done
	;;
fedora | debian | ubuntu | arch)
	run_one "$1"
	;;
*)
	echo "usage: $0 fedora|debian|ubuntu|arch|all" >&2
	exit 2
	;;
esac
