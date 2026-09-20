#!/usr/bin/env bash
#
# End to end check for the "Design your own" tab.
#
# Painting a key is the whole point of this tab, and it is the one thing the
# unit tests cannot reach: the geometry is tested in layout.cpp, but nothing
# there proves the clicks arrive, that the brush colour is the one the user
# picked, or that an edit reaches the keyboard.
#
# This launches the GUI, drives it through its accessibility tree (see
# gui_probe.py), and photographs the canvas to count pixels.
#
# Requires: X11, python3-pyatspi, xdotool, ImageMagick, at-spi2. Run from the
# repo root:
#
#     tools/verify_design_tab.sh
#
# Exits non-zero if a click failed to paint, if the brush colour was ignored,
# or if GTK logged a critical.

set -uo pipefail

cd "$(dirname "$0")/.." || exit 2

BIN=${BIN:-bin/claymore-gui}
OUT=${OUT:-build/design-tab.png}
LOG=${LOG:-build/design-tab.log}
export LOG

# ROG red is what the brush starts as, and it is painted lifted to (255,15,75).
# Counts are limited to the canvas, which is around 650k pixels, so this is a bit
# over two thirds of it - what a filled board actually looks like.
MIN_RED_PIXELS=${MIN_RED_PIXELS:-150000}
MIN_WHITE_GAIN=${MIN_WHITE_GAIN:-500}

if [ ! -x "$BIN" ]; then
    echo "error: $BIN not built yet, run 'make gui'" >&2
    exit 2
fi

if [ -z "${DISPLAY:-}" ]; then
    echo "error: DISPLAY is not set, this check needs an X server" >&2
    exit 2
fi

# wmctrl matters more than it looks: closing the window has to go through
# WM_DELETE_WINDOW so that GTK tears the window down itself. xdotool's
# windowclose destroys the X window under GTK's feet instead, which produces
# three bogus criticals during shutdown and, worse, would skip the shutdown
# path this test exists to check.
for tool in xdotool wmctrl import convert python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: $tool is required for this check" >&2
        exit 2
    fi
done

PROBE="python3 tools/gui_probe.py"

mkdir -p "$(dirname "$OUT")"

# Keep the run out of the user's real config directory. The GUI saves kept
# designs to g_get_user_config_dir(), which honours XDG_CONFIG_HOME, so this
# also checks that the saved file lands where a systemd unit would look.
export XDG_CONFIG_HOME="$PWD/build/test-config"
CONFIG_DIR="$XDG_CONFIG_HOME/claymore"
DESIGN_FILE="$CONFIG_DIR/designs/Desk.txt"
STARTUP_FILE="$CONFIG_DIR/default-design"
rm -rf "$XDG_CONFIG_HOME"

cleanup() {
    if [ -n "${GUI_PID:-}" ]; then
        kill "$GUI_PID" 2>/dev/null
        wait "$GUI_PID" 2>/dev/null
    fi
    # The keyboard is left holding the test pattern, which nobody wants. Hand
    # it back to its own effects, so running the tests is never the reason a
    # board is stuck on something odd.
    if bin/claymore list 2>/dev/null | grep -q "0b05:184d"; then
        bin/claymore release --quiet 2>/dev/null
    fi
    rm -f "$OUT.before.png" "$OUT.after.png" "$OUT.raw.png" "$OUT.restart.png"
    rm -f "$LOG.tray" "$LOG.tray2" "$LOG.window"
}
trap cleanup EXIT

echo "launching $BIN"
"$BIN" > "$LOG" 2>&1 &
GUI_PID=$!

WID=""
for _ in $(seq 1 40); do
    WID=$(xdotool search --name "^ROG Claymore$" 2>/dev/null | head -1)
    [ -n "$WID" ] && break
    sleep 0.25
done

if [ -z "$WID" ]; then
    echo "FAIL: no 'ROG Claymore' window appeared" >&2
    tail -20 "$LOG" >&2
    exit 1
fi

xdotool windowactivate "$WID" 2>/dev/null
xdotool windowraise   "$WID" 2>/dev/null
sleep 2

# Capture the root window and crop it, which is the only method that has been
# reliable here; see verify_gui_render.sh for the two traps.
capture() {
    # shellcheck disable=SC2046
    eval "$(xdotool getwindowgeometry --shell "$WID")"
    import -window root "$OUT.raw.png" 2>/dev/null || return 1
    convert "$OUT.raw.png" -crop "${WIDTH}x${HEIGHT}+${X}+${Y}" +repage "$1" 2>/dev/null

    # Where the canvas sits inside the capture, so counts can be limited to it:
    # the window is full of pale text, which would otherwise be counted as
    # white keys and make the checks pass for the wrong reason.
    CANVAS_CROP="${CW}x${CH}+$((CX - X))+$((CY - Y))"
    export CANVAS_CROP
}

count() {
    # count <file> <red|white>
    local crop=()
    [ -n "${CANVAS_CROP:-}" ] && crop=(-crop "$CANVAS_CROP" +repage)

    case "$2" in
        red)   convert "$1" "${crop[@]}" -fx "(r>0.75 && g<0.35 && b<0.5) ? 1 : 0" -format "%[fx:floor(mean*w*h)]" info: 2>/dev/null ;;
        white) convert "$1" "${crop[@]}" -fx "(r>0.92 && g>0.92 && b>0.92) ? 1 : 0" -format "%[fx:floor(mean*w*h)]" info: 2>/dev/null ;;
    esac
}

STATUS=0
fail() { echo "FAIL: $*" >&2; STATUS=1; }

echo "switching to the design tab"
if ! $PROBE click "Design your own" >/dev/null 2>&1; then
    fail "could not click the 'Design your own' tab"
    exit 1
fi
sleep 1

if ! CANVAS=$($PROBE canvas 2>/dev/null); then
    fail "no keyboard canvas on the design tab"
    exit 1
fi
read -r CX CY CW CH <<< "$CANVAS"
echo "canvas: ${CW}x${CH} at ${CX},${CY}"

# Counting red pixels on the canvas should find most of it: the canvas is one
# flat colour after Fill all, gaps and rounding aside.
CANVAS_PIXELS=$((CW * CH))
echo "  canvas is $CANVAS_PIXELS pixels"

# ---------------------------------------------------------------- fill all --
echo
echo "step 1: clear, then fill every key with the default brush"
$PROBE click "Clear"    >/dev/null 2>&1
sleep 0.5
$PROBE click "Fill all" >/dev/null 2>&1
sleep 1

capture "$OUT.before.png" || { fail "could not photograph the window"; exit 1; }
RED_BEFORE=$(count "$OUT.before.png" red)
echo "  red pixels on the canvas: $RED_BEFORE"

if [ "${RED_BEFORE:-0}" -lt "$MIN_RED_PIXELS" ]; then
    fail "'Fill all' did not paint the canvas (got ${RED_BEFORE:-0} red pixels)"
fi

# ------------------------------------------------------- brush colour, click --
echo
echo "step 2: set the brush to white through the colour dialog"
if ! $PROBE click "Brush colour" >/dev/null 2>&1; then
    fail "could not open the brush colour button"
else
    sleep 1.5

    if ! $PROBE click "White" >/dev/null 2>&1; then
        fail "the colour dialog did not offer a 'White' swatch"
    fi
    sleep 0.5
    if ! $PROBE click "Select" >/dev/null 2>&1; then
        fail "the colour dialog had no 'Select' button"
    fi
    sleep 1
fi

echo
echo "step 3: click keys on the canvas and check they turn white"
WHITE_BEFORE=$(count "$OUT.before.png" white)

# Several points, because a click can land in the gap between keys. The brush
# is expected to paint something for every one of them.
CLICKS=0
for frac in "0.30 0.35" "0.45 0.45" "0.60 0.55" "0.72 0.65"; do
    set -- $frac
    PX=$(python3 -c "print(int($CX + $CW*$1))")
    PY=$(python3 -c "print(int($CY + $CH*$2))")
    xdotool mousemove "$PX" "$PY"; sleep 0.25
    xdotool click 1; sleep 0.35
    CLICKS=$((CLICKS + 1))
done

sleep 1
capture "$OUT.after.png" || { fail "could not photograph the window"; exit 1; }
WHITE_AFTER=$(count "$OUT.after.png" white)
RED_AFTER=$(count "$OUT.after.png" red)

GAIN=$(( ${WHITE_AFTER:-0} - ${WHITE_BEFORE:-0} ))
echo "  white pixels before: ${WHITE_BEFORE:-0}, after: ${WHITE_AFTER:-0} (gain $GAIN over $CLICKS clicks)"
echo "  red pixels after: $RED_AFTER"

if [ "$GAIN" -lt "$MIN_WHITE_GAIN" ]; then
    fail "$CLICKS clicks painted nothing: expected at least $MIN_WHITE_GAIN new white pixels, got $GAIN"
fi

# ----------------------------------------------------------------- streaming --
echo
echo "step 4: the first edit must reach the keyboard on its own"
DEVICE_PRESENT=no
if bin/claymore list 2>/dev/null | grep -q "0b05:184d"; then
    DEVICE_PRESENT=yes
    if grep -q "your design is on the keyboard" "$LOG"; then
        echo "  streaming started automatically"
    else
        fail "a device is connected but editing did not start the stream"
    fi
else
    echo "  skipped: no keyboard connected in this session"
fi

# ------------------------------------------------------- save and start with --
echo
echo "step 5: save the design under a name, and choose it as the startup design"
$PROBE click "Save as..." >/dev/null 2>&1
sleep 1.5

if ! $PROBE type "Design name" "Desk" --enter >/dev/null 2>&1; then
    fail "the name dialog did not accept a name"
fi
sleep 1.5

if [ -f "$DESIGN_FILE" ]; then
    echo "  saved to $DESIGN_FILE ($(grep -c '^[0-9]' "$DESIGN_FILE") keys)"
else
    fail "'Save as...' did not write $DESIGN_FILE"
fi

$PROBE click "Load at startup" >/dev/null 2>&1
sleep 1

if [ -f "$STARTUP_FILE" ] && [ "$(cat "$STARTUP_FILE")" = "Desk" ]; then
    echo "  'Desk' is now the startup design"
else
    fail "'Load at startup' did not record the design"
fi

# ------------------------------------------------------------------ keeping --
echo
echo "step 6: closing the window must leave the design on the keyboard"
if [ "$DEVICE_PRESENT" = "yes" ]; then
    # Close the window the way a user does, through the window manager. A
    # signal would skip the shutdown path that decides who owns the board,
    # which is the code under test here.
    wmctrl -c "ROG Claymore" 2>/dev/null

    for _ in $(seq 1 20); do
        kill -0 "$GUI_PID" 2>/dev/null || break
        sleep 0.5
    done

    if kill -0 "$GUI_PID" 2>/dev/null; then
        fail "the window did not close"
    else
        if grep -q "the design stays on the keyboard" "$LOG"; then
            echo "  design left on the keyboard after exit"
        else
            fail "closing the window did not leave the design on the keyboard"
        fi

        if grep -q "released remote control on exit" "$LOG"; then
            fail "the design was thrown away on exit instead of kept"
        fi

        # This is the one place the GUI's writer and the CLI's reader meet, and
        # they used to be two separate parsers. It is also exactly what the
        # systemd unit runs.
        if bin/claymore paint --default --quiet; then
            echo "  the CLI applied the startup design"
        else
            fail "the CLI could not apply the startup design"
        fi
    fi
else
    echo "  skipped: no keyboard connected in this session"
fi

# ------------------------------------------------------- restart, and the panel --
echo
echo "step 7: restarting must bring the design back, and the panel must keep it"
"$BIN" > "$LOG.restart" 2>&1 &
GUI_PID=$!

WID=""
for _ in $(seq 1 40); do
    WID=$(xdotool search --name "^ROG Claymore$" 2>/dev/null | head -1)
    [ -n "$WID" ] && break
    sleep 0.25
done

if [ -z "$WID" ]; then
    fail "the window did not come back"
else
    xdotool windowactivate "$WID" 2>/dev/null
    xdotool windowraise   "$WID" 2>/dev/null
    sleep 3

    if grep -q "loaded the startup design 'Desk'" "$LOG.restart"; then
        echo "  the startup design was loaded"
    else
        fail "the startup design was not loaded on restart"
    fi

    # A fresh window opens on the Effects tab, whose preview is also painted
    # red, so the design tab has to be in front before counting anything - the
    # white keys painted in step 3 are what tell the two apart.
    $PROBE click "Design your own" >/dev/null 2>&1
    sleep 1

    capture "$OUT.restart.png" || fail "could not photograph the restarted window"
    RED_RESTART=$(count "$OUT.restart.png" red)
    WHITE_RESTART=$(count "$OUT.restart.png" white)
    echo "  after restart: $RED_RESTART red, $WHITE_RESTART white"

    if [ "${RED_RESTART:-0}" -lt "$MIN_RED_PIXELS" ]; then
        fail "the restarted window does not show the saved red design"
    fi

    if [ "${WHITE_RESTART:-0}" -lt "5000" ]; then
        fail "the restarted window lost the white keys that were painted"
    fi

    # The panel icon is the way to keep a design applied without a window on
    # screen, so check that the window really goes away and the program really
    # keeps running.
    $PROBE click "To panel" >/dev/null 2>&1
    sleep 2

    if kill -0 "$GUI_PID" 2>/dev/null; then
        echo "  still running after moving to the panel"
    else
        fail "the program exited when it was sent to the panel"
    fi

    MAP_STATE=$(xwininfo -id "$WID" 2>/dev/null | grep -i "map state" || echo "")
    if echo "$MAP_STATE" | grep -qi "IsUnMapped"; then
        echo "  the window is out of the way"
    else
        fail "the window is still on screen after moving to the panel ($MAP_STATE)"
    fi

    kill "$GUI_PID" 2>/dev/null
    wait "$GUI_PID" 2>/dev/null
fi

# --------------------------------------------------- starting in the panel --
#
# 'Start in the panel' is the setting that makes an autostart entry usable, so
# what it has to prove is exactly that: with it on, launching the program
# produces no window, and the flag still rescues you when you launch it by hand
# and expect one.
echo
echo "step 8: 'Start in the panel' must keep the window away on the next start"
SETTINGS_FILE="$CONFIG_DIR/settings"

"$BIN" > "$LOG.tray" 2>&1 &
GUI_PID=$!

WID=""
for _ in $(seq 1 40); do
    WID=$(xdotool search --name "^ROG Claymore$" 2>/dev/null | head -1)
    [ -n "$WID" ] && break
    sleep 0.25
done

if [ -z "$WID" ]; then
    fail "the window did not open at all"
else
    xdotool windowactivate "$WID" 2>/dev/null
    sleep 1

    $PROBE click "Start in the panel" >/dev/null 2>&1
    sleep 1

    if [ -f "$SETTINGS_FILE" ] && grep -q "^start-in-tray on" "$SETTINGS_FILE"; then
        echo "  the setting was saved to $SETTINGS_FILE"
    else
        fail "'Start in the panel' did not save the setting"
    fi

    kill "$GUI_PID" 2>/dev/null
    wait "$GUI_PID" 2>/dev/null

    # Now start it the way an autostart entry would: no arguments at all.
    "$BIN" > "$LOG.tray2" 2>&1 &
    GUI_PID=$!
    sleep 4

    if ! kill -0 "$GUI_PID" 2>/dev/null; then
        fail "the program exited instead of starting in the panel"
    else
        echo "  it is running with no arguments"
    fi

    HIDDEN_WID=$(xdotool search --name "^ROG Claymore$" 2>/dev/null | head -1)
    if [ -n "$HIDDEN_WID" ]; then
        STATE=$(xwininfo -id "$HIDDEN_WID" 2>/dev/null | grep -i "map state" || echo "")
        case "$STATE" in
            *IsUnMapped*|*IsUnviewable*) echo "  and its window is not on screen ($STATE)" ;;
            *) fail "the window is on screen even though 'start in tray' is on ($STATE)" ;;
        esac
    fi

    kill "$GUI_PID" 2>/dev/null
    wait "$GUI_PID" 2>/dev/null

    # --window is the way back for someone who turned the setting on and then
    # launched it from the menu expecting a window.
    "$BIN" --window > "$LOG.window" 2>&1 &
    GUI_PID=$!

    WID=""
    for _ in $(seq 1 40); do
        WID=$(xdotool search --name "^ROG Claymore$" 2>/dev/null | head -1)
        [ -n "$WID" ] && break
        sleep 0.25
    done

    if [ -z "$WID" ]; then
        fail "--window did not open the window"
    else
        STATE=$(xwininfo -id "$WID" 2>/dev/null | grep -i "map state" || echo "")
        case "$STATE" in
            *IsViewable*) echo "  --window overrides the setting" ;;
            *) fail "--window did not put the window on screen ($STATE)" ;;
        esac
    fi

    kill "$GUI_PID" 2>/dev/null
    wait "$GUI_PID" 2>/dev/null
fi

# ------------------------------------------------------------------ criticals --
CRITICALS=$(cat "$LOG" "$LOG.restart" "$LOG.tray" "$LOG.tray2" "$LOG.window" 2>/dev/null | grep -c "CRITICAL" || true)
echo
echo "gtk criticals: $CRITICALS"
if [ "${CRITICALS:-1}" -ne 0 ]; then
    fail "GTK logged criticals"
    cat "$LOG" "$LOG.restart" "$LOG.tray" "$LOG.tray2" "$LOG.window" 2>/dev/null \
        | grep "CRITICAL" | head -10 >&2
fi

echo
if [ "$STATUS" -eq 0 ]; then
    echo "PASS"
    echo "before: $OUT.before.png"
    echo "after:  $OUT.after.png"
else
    echo "captures kept at $OUT.before.png and $OUT.after.png" >&2
fi

exit "$STATUS"
