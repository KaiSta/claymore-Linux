#!/usr/bin/env bash
#
# Headless-ish smoke test for the GUI.
#
# The keyboard canvas is the one piece of this project that cannot be unit
# tested, and it is easy to ship a window that opens and shows nothing. This
# launches the GUI, photographs the window, and asserts that the canvas really
# painted keys in the expected colour.
#
# Requires: X11, xdotool, xwd, ImageMagick. Run with the repo root as cwd:
#
#     tools/verify_gui_render.sh
#
# Exits non-zero if the canvas did not paint, or if GTK logged a critical.

set -uo pipefail

BIN=${BIN:-bin/claymore-gui}
OUT=${OUT:-build/gui-render.png}
LOG=${LOG:-build/gui-render.log}

# A preset chosen because it paints every key one flat, easily counted colour.
PRESET=${PRESET:-"rog-red"}
MIN_RED_PIXELS=${MIN_RED_PIXELS:-40000}

if [ ! -x "$BIN" ]; then
    echo "error: $BIN not built yet, run 'make gui'" >&2
    exit 2
fi

if [ -z "${DISPLAY:-}" ]; then
    echo "error: DISPLAY is not set, this check needs an X server" >&2
    exit 2
fi

mkdir -p "$(dirname "$OUT")"

cleanup() {
    if [ -n "${GUI_PID:-}" ]; then
        kill "$GUI_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

echo "launching $BIN"
"$BIN" > "$LOG" 2>&1 &
GUI_PID=$!

# The preset list is selected on startup, so the canvas has content within a
# second or two. Wait for the window rather than sleeping a fixed amount.
WID=""
for _ in $(seq 1 40); do
    WID=$(xdotool search --name "^Claymore@Linux$" 2>/dev/null | head -1)
    [ -n "$WID" ] && break
    sleep 0.25
done

if [ -z "$WID" ]; then
    echo "FAIL: no 'Claymore@Linux' window appeared" >&2
    tail -20 "$LOG" >&2
    exit 1
fi

sleep 2
xdotool windowactivate "$WID" 2>/dev/null
xdotool windowraise "$WID" 2>/dev/null
sleep 2

# Capture the root window and crop, rather than capturing the window id
# directly. Two traps here, both learned the hard way:
#
#   * xwd -id on an obscured window hands back whatever is painted on top
#     of it, and xwd -root can fail outright on a mismatched colormap.
#   * ImageMagick's `import -window <id>` silently falls back to the whole
#     screen when given an id it does not like, which yields a crop of the
#     desktop wallpaper and a meaningless result.
#
# Root capture plus an explicit crop is boring and correct.
#
# shellcheck disable=SC2046
eval "$(xdotool getwindowgeometry --shell "$WID")"

if [ -z "${WIDTH:-}" ] || [ -z "${HEIGHT:-}" ]; then
    echo "FAIL: could not read the window geometry" >&2
    exit 1
fi

if ! import -window root "$OUT.raw.png" 2>/dev/null; then
    echo "FAIL: could not capture the screen" >&2
    exit 1
fi

convert "$OUT.raw.png" -crop "${WIDTH}x${HEIGHT}+${X}+${Y}" +repage "$OUT" 2>/dev/null
rm -f "$OUT.raw.png"

if [ ! -f "$OUT" ]; then
    echo "FAIL: crop produced no image" >&2
    exit 1
fi

echo "window: ${WIDTH}x${HEIGHT}+${X}+${Y}"
echo "capture: $OUT ($(identify -format '%wx%h' "$OUT"))"

# ROG red is painted as FF003C per key, which the canvas lifts slightly so that
# dark designs still show key outlines, giving roughly (255, 15, 75).
RED=$(convert "$OUT" -fx "(r>0.75 && g<0.35 && b<0.5) ? 1 : 0" \
      -format "%[fx:floor(mean*w*h)]" info: 2>/dev/null)

DISTINCT=$(convert "$OUT" -format %k info: 2>/dev/null)
CRITICALS=$(grep -c "CRITICAL" "$LOG" 2>/dev/null || true)

echo "red-ish pixels: $RED (need at least $MIN_RED_PIXELS)"
echo "distinct colours: $DISTINCT"
echo "gtk criticals: $CRITICALS"

STATUS=0

if [ "${CRITICALS:-1}" -ne 0 ]; then
    echo "FAIL: GTK logged criticals:" >&2
    grep "CRITICAL" "$LOG" | head -10 >&2
    STATUS=1
fi

if [ "${RED:-0}" -lt "$MIN_RED_PIXELS" ]; then
    echo "FAIL: the keyboard canvas does not appear to have painted the preset." >&2
    echo "      That usually means a canvas was built before the layout was known," >&2
    echo "      see attach_views() in gui/app.cpp." >&2
    STATUS=1
fi

if [ "$STATUS" -eq 0 ]; then
    echo "PASS"
    echo
    echo "Look at $OUT to see the window as rendered."
else
    echo "capture kept at $OUT for inspection" >&2
fi

exit "$STATUS"
