#!/bin/sh
# --------------------------------------------------------------- *\
# | build_deb.sh - turn an install into a .deb                        |
# |                                                                   |
# |   Part of rog-claymore-linux                                      |
# |   SPDX-License-Identifier: GPL-2.0-or-later                       |
# |                                                                   |
# | Called by 'make package'. The package is built from what          |
# | 'make install' stages, into a PREFIX of /usr: a package that      |
# | installs something other than the install target does is the      |
# | classic way for the two to disagree, so there is only one list.   |
# |                                                                   |
# | dpkg-deb is the only tool needed - no debhelper, no dh_make,      |
# | nothing to install beyond what a Debian machine already has.      |
# \*----------------------------------------------------------------- */

set -eu

VERSION=${1:?usage: build_deb.sh VERSION ARCH STAGING_DIR OUTPUT_DEB}
ARCH=${2:?usage: build_deb.sh VERSION ARCH STAGING_DIR OUTPUT_DEB}
ROOT=${3:?usage: build_deb.sh VERSION ARCH STAGING_DIR OUTPUT_DEB}
OUTPUT=${4:?usage: build_deb.sh VERSION ARCH STAGING_DIR OUTPUT_DEB}

MAINTAINER=${DEB_MAINTAINER:-$(id -un) <$(id -un)@$(hostname 2>/dev/null || echo localhost)>}

# The script is run from wherever make was started, so anchor it.
cd "$(dirname "$0")/.."

for tool in dpkg-deb gzip md5sum; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "build_deb.sh: $tool is needed to build a package" >&2
        exit 1
    fi
done

echo "==> staging $VERSION for $ARCH into $ROOT"
rm -rf "$ROOT"
mkdir -p "$ROOT/DEBIAN"

# PREFIX=/usr, not /usr/local: a package owns /usr.
make install DESTDIR="$ROOT" PREFIX=/usr >/dev/null

installed_kb=$(du -sk "$ROOT/usr" | cut -f1)

# --------------------------------------------------------------- control --
#
# libgtk-3-0t64 is what Ubuntu 24.04 and Debian trixie call GTK 3; older
# releases call it libgtk-3-0. Listing both as alternatives means installing
# this on either does not fight the dependency resolver over a package name
# that changed underneath it.
cat > "$ROOT/DEBIAN/control" <<EOF
Package: rog-claymore
Version: $VERSION
Architecture: $ARCH
Maintainer: $MAINTAINER
Section: utils
Priority: optional
Depends: libc6 (>= 2.34), libgtk-3-0t64 | libgtk-3-0
Suggests: systemd
Installed-Size: $installed_kb
Description: lighting control for the ASUS ROG Claymore on Linux
 Effects, per-key designs and an Aura Sync style socket for the ASUS ROG
 Claymore, without the vendor software.
 .
 The command line tool drives the keyboard directly through /dev/hidraw with
 no dependencies beyond libc: firmware effects, per-key painting, saved
 designs and a sync daemon. The GTK 3 interface adds an animated preview,
 a design editor and a panel icon that keeps a design applied.
 .
 Everything it does was verified against a real Claymore; docs/PROTOCOL.md
 records the packet format and the two undocumented quirks it depends on.
EOF

# -------------------------------------------------------------- maintainer --
#
# The udev rule is the whole reason this needs to run anything after unpacking:
# without a reload the keyboard stays root-only and the program cannot open it.
cat > "$ROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e

if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules || true
    # Apply it to the keyboard now rather than asking for a replug.
    udevadm trigger --subsystem-match=hidraw --action=change || true
fi

update-desktop-database -q /usr/share/applications 2>/dev/null || true
gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor 2>/dev/null || true

exit 0
EOF

cat > "$ROOT/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e

if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules || true
fi

update-desktop-database -q /usr/share/applications 2>/dev/null || true
gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor 2>/dev/null || true

exit 0
EOF

chmod 755 "$ROOT/DEBIAN/postinst" "$ROOT/DEBIAN/postrm"

# ---------------------------------------------------------------- md5sums --
#
# Generated rather than omitted, so the package is complete enough to be
# inspected with the usual tools.
( cd "$ROOT" && find . -path ./DEBIAN -prune -o -type f -print \
    | sed 's|^\./||' | sort | xargs -r md5sum ) > "$ROOT/DEBIAN/md5sums"

mkdir -p "$(dirname "$OUTPUT")"

# --root-owner-group keeps the build user's uid out of the archive, so the
# package does not depend on who built it.
echo "==> building $OUTPUT"
dpkg-deb --root-owner-group --build "$ROOT" "$OUTPUT"

echo
dpkg-deb --info "$OUTPUT" | sed -n '1,20p'
echo
echo "contents:"
dpkg-deb --contents "$OUTPUT" | sed -n '2,40p'
echo
echo "install it with:  sudo apt install ./$(basename "$OUTPUT")"
