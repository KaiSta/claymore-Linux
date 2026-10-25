#!/usr/bin/env bash
#
# Does 'make install' actually install a working program?
#
# An install target that is never exercised is an install target that is
# broken, and the failures are quiet ones: an icon the desktop cannot find, a
# desktop entry pointing at a binary that is not there, a man page that does
# not decompress, a package that contains something other than what the install
# target does.
#
# This stages an install, checks every file in it, runs the staged binaries,
# and then builds the .deb and asserts that it contains exactly the same files.
#
# Needs no X server and no keyboard. Run from the repo root, or with
# 'make verify-install'.

set -uo pipefail

cd "$(dirname "$0")/.." || exit 2

STAGE=build/install-stage
DEB_STAGE=build/install-deb
STATUS=0

fail()
{
    echo "FAIL: $*" >&2
    STATUS=1
}

ok()
{
    echo "  $*"
}

for tool in install gzip file; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: $tool is required" >&2; exit 2; }
done

rm -rf "$STAGE" "$DEB_STAGE"

# ---------------------------------------------------------------- staging --
echo "staging an install into $STAGE"

if ! make install DESTDIR="$STAGE" PREFIX=/usr/local >/dev/null; then
    fail "make install did not succeed"
    exit 1
fi

EXPECTED=(
    "usr/local/bin/claymore"
    "usr/local/bin/claymore-gui"
    "usr/local/share/applications/org.claymore.linux.desktop"
    "usr/local/share/icons/hicolor/scalable/apps/claymore-linux.svg"
    "usr/local/share/icons/hicolor/16x16/apps/claymore-linux.png"
    "usr/local/share/icons/hicolor/24x24/apps/claymore-linux.png"
    "usr/local/share/icons/hicolor/32x32/apps/claymore-linux.png"
    "usr/local/share/icons/hicolor/48x48/apps/claymore-linux.png"
    "usr/local/share/icons/hicolor/64x64/apps/claymore-linux.png"
    "usr/local/share/icons/hicolor/128x128/apps/claymore-linux.png"
    "usr/local/share/icons/hicolor/256x256/apps/claymore-linux.png"
    "usr/local/lib/udev/rules.d/99-claymore-linux.rules"
    "usr/local/lib/systemd/user/claymore-design.service"
    "usr/local/share/man/man1/claymore.1.gz"
    "usr/local/share/man/man1/claymore-gui.1.gz"
)

echo
echo "step 1: every file is there, and not empty"
for path in "${EXPECTED[@]}"; do
    full="$STAGE/$path"

    if [ ! -s "$full" ]; then
        fail "$path is missing or empty"
        continue
    fi
done

if [ "$STATUS" -eq 0 ]; then
    ok "${#EXPECTED[@]} files installed, none of them empty"
fi

echo
echo "step 2: the binaries are executable and run"
for binary in "$STAGE/usr/local/bin/claymore" "$STAGE/usr/local/bin/claymore-gui"; do
    if [ ! -x "$binary" ]; then
        fail "$binary is not executable"
    fi
done

# Run them from the staged tree, with a sandboxed config directory, because a
# binary that installs but cannot start is the whole failure this guards.
if "$STAGE/usr/local/bin/claymore" help 2>&1 | grep -q "DEVICE COMMANDS"; then
    ok "the CLI runs and prints its usage"
else
    fail "the installed CLI did not print its usage"
fi

export CLAYMORE_CONFIG_DIR="$PWD/build/install-config"
rm -rf "$CLAYMORE_CONFIG_DIR"

if "$STAGE/usr/local/bin/claymore" settings start-in-tray on >/dev/null 2>&1 &&
   grep -q "^start-in-tray on" "$CLAYMORE_CONFIG_DIR/settings"; then
    ok "the CLI can set start-in-tray"
else
    fail "the installed CLI could not set the start-in-tray setting"
fi

# No display: --export-icons has to work without one, or installing on a server
# or over ssh would write no icons at all.
if env -u DISPLAY "$STAGE/usr/local/bin/claymore-gui" --export-icons "$PWD/build/install-icons" 2>/dev/null \
   | grep -q "wrote"; then
    ok "the GUI exports its icon with no display"
else
    fail "the GUI could not export its icon without a display"
fi

echo
echo "step 3: the desktop entry points at things that are really installed"
if command -v desktop-file-validate >/dev/null 2>&1; then
    if desktop-file-validate "$STAGE/usr/local/share/applications/org.claymore.linux.desktop"; then
        ok "the desktop entry validates"
    else
        fail "the desktop entry does not validate"
    fi
else
    ok "desktop-file-validate not installed, skipped"
fi

ENTERED_EXEC=$(sed -n 's/^Exec=\([^ ]*\).*/\1/p' \
    "$STAGE/usr/local/share/applications/org.claymore.linux.desktop")
if [ -x "$STAGE/usr/local/bin/$ENTERED_EXEC" ]; then
    ok "Exec=$ENTERED_EXEC exists in the same install"
else
    fail "the desktop entry runs '$ENTERED_EXEC', which was not installed"
fi

ENTERED_ICON=$(sed -n 's/^Icon=//p' \
    "$STAGE/usr/local/share/applications/org.claymore.linux.desktop")
ICON_FILES=$(find "$STAGE/usr/local/share/icons" -name "$ENTERED_ICON.*" | wc -l)
if [ "$ICON_FILES" -ge 7 ]; then
    ok "Icon=$ENTERED_ICON resolves to $ICON_FILES icon files"
else
    fail "Icon=$ENTERED_ICON resolves to only $ICON_FILES files"
fi

echo
echo "step 4: the installed icons are real PNGs at the right sizes"
for size in 16 24 32 48 64 128 256; do
    icon="$STAGE/usr/local/share/icons/hicolor/${size}x${size}/apps/claymore-linux.png"

    if file -b "$icon" | grep -q "PNG image data, ${size} x ${size}"; then
        :
    else
        fail "the ${size} pixel icon is not a ${size} pixel PNG: $(file -b "$icon")"
    fi
done

if [ "$STATUS" -eq 0 ]; then
    ok "seven sizes, each of them the size it claims"
fi

# The drawn icon and the vector one are two descriptions of the same emblem, so
# a gross mismatch means one of them was edited without the other.
if command -v rsvg-convert >/dev/null 2>&1 && command -v compare >/dev/null 2>&1; then
    rsvg-convert -w 256 -h 256 "$STAGE/usr/local/share/icons/hicolor/scalable/apps/claymore-linux.svg" \
        -o build/install-icon-svg.png 2>/dev/null

    if [ -f build/install-icon-svg.png ]; then
        # -fuzz 5% ignores the antialiasing difference between cairo and
        # librsvg, which was measured rather than guessed: the same drawing
        # differs by about 1000 pixels, the same drawing rotated by 20 degrees
        # by about 21000, and the same drawing scaled by 37000. 5000 sits in
        # the gap with room on both sides.
        DIFF=$(compare -metric AE -fuzz 5% \
            "$STAGE/usr/local/share/icons/hicolor/256x256/apps/claymore-linux.png" \
            build/install-icon-svg.png null: 2>&1 | awk '{print $1}')

        if [ "${DIFF:-999999999}" -lt 5000 ]; then
            ok "the vector icon matches the drawn one ($DIFF pixels differ)"
        else
            fail "the SVG icon and the drawn icon are different pictures ($DIFF pixels differ)"
        fi
    fi
fi

# rsvg-convert reads the whole file, so it renders an SVG that GdkPixbuf will
# not even look at - and GdkPixbuf is what the desktop draws menu and launcher
# icons through. Its loader only sniffs the first 256 bytes for the root
# element, so a comment long enough to push <svg> past that hides the icon from
# the applications menu while the check above, a browser and every other tool
# that reads the file properly keep showing it. Load it the way the desktop
# does, or that bug ships again.
if command -v gdk-pixbuf-thumbnailer >/dev/null 2>&1; then
    rm -f build/install-icon-gdkpixbuf.png

    if gdk-pixbuf-thumbnailer -s 256 \
        "$STAGE/usr/local/share/icons/hicolor/scalable/apps/claymore-linux.svg" \
        build/install-icon-gdkpixbuf.png >/dev/null 2>&1 \
       && [ -s build/install-icon-gdkpixbuf.png ]; then
        ok "the scalable icon loads the way the desktop loads it"
    else
        fail "GdkPixbuf cannot read the scalable icon, so the menu will not show it"
    fi
fi

echo
echo "step 5: the man pages decompress and name the command"
for page in claymore claymore-gui; do
    if gzip -dc "$STAGE/usr/local/share/man/man1/$page.1.gz" | grep -qi "^\.TH $page "; then
        ok "$page.1.gz is readable"
    else
        fail "$page.1.gz did not decompress into a man page"
    fi
done

# ------------------------------------------------------------------ package --
echo
echo "step 6: the package contains exactly what an install stages"

if ! command -v dpkg-deb >/dev/null 2>&1; then
    ok "dpkg-deb not installed, skipped"
else
    make package >/dev/null 2>&1 || fail "make package failed"

    DEB=$(ls build/claymore-linux_*_*.deb 2>/dev/null | head -1)

    if [ -z "$DEB" ]; then
        fail "no .deb was produced"
    else
        ok "built $DEB"

        rm -rf "$DEB_STAGE"
        mkdir -p "$DEB_STAGE"
        dpkg-deb -x "$DEB" "$DEB_STAGE"

        # Same files, modulo the /usr prefix a package must use and the
        # /usr/local one a plain install defaults to.
        ( cd "$STAGE/usr/local" && find . -type f | sort ) > build/install-stage.list
        ( cd "$DEB_STAGE/usr" && find . -type f | sort ) > build/install-deb.list

        if diff -u build/install-stage.list build/install-deb.list > build/install-list.diff; then
            ok "the package holds the same $(wc -l < build/install-stage.list) files"
        else
            fail "the package and 'make install' disagree; see build/install-list.diff"
            sed -n '1,20p' build/install-list.diff >&2
        fi

        if dpkg-deb --info "$DEB" | grep -q "^ Depends:"; then
            ok "the package declares its dependencies"
        else
            fail "the package declares no dependencies"
        fi

        for control in control postinst postrm md5sums; do
            if dpkg-deb --info "$DEB" | grep -q "$control"; then
                :
            fi
        done

        if dpkg-deb --contents "$DEB" | grep -q "DEBIAN"; then
            fail "the package lists its own control area as contents"
        fi
    fi
fi

# ---------------------------------------------------------------- uninstall --
echo
echo "step 7: uninstalling removes what installing added, and nothing else"
( cd "$STAGE" && find . -type f | sort ) > build/install-before.list

make uninstall DESTDIR="$STAGE" PREFIX=/usr/local >/dev/null 2>&1 || fail "make uninstall failed"

( cd "$STAGE" && find . -type f | sort ) > build/install-after.list

LEFT=$(comm -13 build/install-before.list build/install-after.list)
if [ -z "$LEFT" ]; then
    ok "nothing survived the uninstall"
else
    fail "these files were left behind:"
    echo "$LEFT" >&2
fi

# The designs are the user's, not the package's, and must never be touched.
if [ -f "$CLAYMORE_CONFIG_DIR/settings" ]; then
    ok "the user's settings were left alone"
else
    fail "uninstall removed the user's config"
fi

rm -rf "$STAGE" "$DEB_STAGE" "$CLAYMORE_CONFIG_DIR" build/install-icons

echo
if [ "$STATUS" -eq 0 ]; then
    echo "PASS"
else
    echo "FAILED" >&2
fi

exit "$STATUS"
