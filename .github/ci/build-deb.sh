#!/usr/bin/env bash
#
# Build the .deb inside a Debian-family container, for the container's own
# architecture. Called by .github/workflows/release.yml with VERSION and ARCH
# in the environment, and runnable by hand for the same package:
#
#   docker run --rm -v "$PWD:/src" -w /src \
#       -e VERSION="$(cat VERSION)" -e ARCH=amd64 \
#       ubuntu:24.04 bash .github/ci/build-deb.sh
#
# For the other architecture, add --platform linux/arm64 and ARCH=arm64 (and
# register qemu first: docker run --privileged --rm tonistiigi/binfmt --install arm64).
#
# The point of building on the target architecture rather than cross-compiling
# is the GUI: claymore-gui has to link against the GTK of the architecture it
# ships for, and the tests have to run there before the package exists.

set -eux

: "${VERSION:?VERSION is required, e.g. VERSION=$(cat VERSION 2>/dev/null || echo 1.0.0)}"
: "${ARCH:?ARCH is required, e.g. ARCH=amd64 or ARCH=arm64}"

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    build-essential \
    pkg-config \
    libgtk-3-dev \
    dpkg-dev \
    gzip \
    coreutils \
    ca-certificates

# The GUI is part of the package. If GTK 3 is missing here the build silently
# produces a CLI-only .deb, so stop instead of shipping a package that lost
# half its contents.
pkg-config --exists gtk+-3.0

make test
make package VERSION="$VERSION" DEB_ARCH="$ARCH"

DEB="build/claymore-linux_${VERSION}_${ARCH}.deb"

# The name of the file is not evidence: ask the package what it is.
test -f "$DEB"
test "$(dpkg-deb -f "$DEB" Architecture)" = "$ARCH"
test "$(dpkg-deb -f "$DEB" Version)" = "$VERSION"
dpkg-deb --contents "$DEB" | grep -q './usr/bin/claymore-gui'

ls -l "$DEB"

# The container runs as root; give the tree back to the user that owns the
# checkout, so later steps and the runner's cleanup are not fighting uid 0.
if [ -n "${HOST_UID:-}" ] && [ -n "${HOST_GID:-}" ]; then
    chown -R "$HOST_UID:$HOST_GID" build bin 2>/dev/null || true
fi
