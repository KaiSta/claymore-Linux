#!/usr/bin/env python3
"""gui_probe.py - drive the Claymore GUI through its accessibility tree.

The keyboard canvas is the one part of this project that unit tests cannot
reach, and earlier bugs (a canvas built before the layout was known, a design
tab that silently ignored clicks) were only visible in the running window.

This uses AT-SPI, which GTK publishes automatically, to find widgets by name
and report their screen coordinates. GTK's own test APIs are not usable here
because the GUI is a separate process, and photographing the window tells you
what was drawn but not which widget is where.

    tools/gui_probe.py dump              # every named widget, with coordinates
    tools/gui_probe.py click "Fill all"  # click a widget by its label
    tools/gui_probe.py canvas            # print the keyboard canvas geometry

Exit status is non-zero when a requested widget is not found, so the output is
safe to use from a shell script.
"""

import argparse
import subprocess
import sys
import time

try:
    import pyatspi
except ImportError:  # pragma: no cover - depends on the host
    print("error: pyatspi is not available (install python3-pyatspi)", file=sys.stderr)
    raise SystemExit(2)

# AT-SPI names the application after the binary, not the window title.
APP_NAMES = ("claymore-gui", "ROG Claymore")


def find_application(deadline=15.0):
    """Return the accessibility node for the GUI window, or None."""
    end = time.time() + deadline
    while time.time() < end:
        desktop = pyatspi.Registry.getDesktop(0)
        for app in desktop:
            try:
                if (app.name or "") in APP_NAMES:
                    return app
            except Exception:
                continue
        time.sleep(0.5)
    return None


def extents(node):
    try:
        component = node.queryComponent()
        if component is None:
            return None
        box = component.getExtents(pyatspi.DESKTOP_COORDS)
        if box.width <= 0 or box.height <= 0:
            return None
        return box
    except Exception:
        return None


def walk(node, depth, rows, max_depth):
    if depth > max_depth:
        return
    try:
        role = node.getRoleName()
        name = node.name or ""
    except Exception:
        return

    box = extents(node)
    rows.append((depth, role, name, box))

    try:
        children = list(node)
    except Exception:
        return

    for child in children:
        walk(child, depth + 1, rows, max_depth)


def describe(rows, show_all):
    for depth, role, name, box in rows:
        if not show_all and not name and role not in ("drawing area", "page tab"):
            continue
        where = f"{box.x:5d},{box.y:5d} {box.width:4d}x{box.height:<4d}" if box else " " * 22
        label = f'"{name}"' if name else ""
        print(f"{'  ' * depth}{where} {role:<16} {label}")


def best_match(rows, wanted):
    """Prefer an exact, visible widget: buttons and tabs before containers."""
    wanted_lower = wanted.lower()
    candidates = []
    for depth, role, name, box in rows:
        if not name or box is None:
            continue
        if name.lower() != wanted_lower:
            continue
        # A clickable widget is what we want; containers could shadow it.
        priority = 0 if role in ("push button", "radio button", "page tab",
                                 "check box", "toggle button") else 1
        candidates.append((priority, -depth, box, role))
    if not candidates:
        return None
    candidates.sort(key=lambda item: (item[0], item[1]))
    return candidates[0]


def click(box):
    x = box.x + box.width // 2
    y = box.y + box.height // 2
    subprocess.run(["xdotool", "mousemove", str(x), str(y)], check=True)
    time.sleep(0.2)
    subprocess.run(["xdotool", "click", "1"], check=True)
    time.sleep(0.3)
    return x, y


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["dump", "click", "canvas", "type"])
    parser.add_argument("target", nargs="?", default="")
    parser.add_argument("text", nargs="?", default="", help="for 'type': the text to enter")
    parser.add_argument("--enter", action="store_true", help="for 'type': press Enter afterwards")
    parser.add_argument("--append", action="store_true",
                        help="for 'type': keep what is there instead of replacing it")
    parser.add_argument("--all", action="store_true", help="also list unnamed containers")
    parser.add_argument("--depth", type=int, default=12)
    args = parser.parse_args()

    app = find_application()
    if app is None:
        print(f"error: no window matching any of {APP_NAMES}", file=sys.stderr)
        return 2

    rows = []
    walk(app, 0, rows, args.depth)

    if args.action == "dump":
        describe(rows, args.all)
        return 0

    if args.action == "canvas":
        # The canvas is the largest drawing area in the window.
        areas = [r for r in rows if r[1] in ("drawing area", "canvas") and r[3] is not None]
        if not areas:
            print("error: no drawing area found", file=sys.stderr)
            return 1
        _, role, name, box = max(areas, key=lambda r: r[3].width * r[3].height)
        print(f"{box.x} {box.y} {box.width} {box.height}")
        return 0

    if not args.target:
        print(f"error: {args.action} needs a widget name", file=sys.stderr)
        return 2

    match = best_match(rows, args.target)
    if match is None:
        print(f"error: no widget named {args.target!r}", file=sys.stderr)
        return 1

    _, _, box, role = match

    if args.action == "type":
        # Focus by clicking, then type for real. Enter a dialog field the way a
        # person would, rather than poking the widget's text buffer: the point
        # is to exercise the same path a user takes.
        click(box)

        # Fields are often pre-filled with a suggestion, so select it first;
        # otherwise typing appends and the name comes out as a merger of the
        # two. Selecting all and then typing is what a person does here.
        if not args.append:
            subprocess.run(["xdotool", "key", "ctrl+a"], check=True)
            time.sleep(0.1)

        subprocess.run(["xdotool", "type", "--delay", "25", "--", args.text], check=True)

        if args.enter:
            subprocess.run(["xdotool", "key", "Return"], check=True)
            time.sleep(0.4)

        print(f"typed {args.text!r} into {role} {args.target!r}")
        return 0

    x, y = click(box)
    print(f"clicked {role} {args.target!r} at {x},{y}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
