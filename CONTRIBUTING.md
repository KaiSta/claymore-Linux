# Contributing to Claymore@Linux

Patches, bug reports and reports from other Claymore firmware revisions are
welcome. This file has the parts that are for people working on the code: how to
build, what is tested and how, how a package and a release are made, and how the
generated layout tables are reproduced. Users who only want their keyboard lit
should read the [README](README.md).

The project is GPL-2.0-or-later; by contributing you agree to license your work
the same way. Keep changes small, match the existing style, and keep the protocol
invariants in [`docs/PROTOCOL.md`](docs/PROTOCOL.md) intact.

## Build

```sh
make            # CLI + GUI + tests
make cli        # just bin/claymore
make gui        # just bin/claymore-gui
make test       # hardware free test suite
make package    # build/claymore-linux_<version>_<arch>.deb, dpkg-deb alone
```

The CLI and the driver need nothing beyond a C++17 compiler and libc. The GUI
needs GTK 3 development headers; if they are missing the GUI target is skipped and
everything else still builds, and `make install` says so instead of quietly
installing half a program.

## Tests and checks

`make test` covers packets, layouts, canvas geometry, design files, the design
store and the settings file, all without hardware. Never make a unit test open a
real keyboard.

The checks below are scripts rather than unit tests: they need `python3`, an X
server, `wmctrl`, ImageMagick and the AT-SPI tooling, because they drive the real
window and read real pixels.

```sh
make verify-docs    # validate AGENTS.md, the code map, and Makefile references
make verify-gui     # launches the GUI, photographs it, checks it painted
make verify-design  # drives the design tab with real clicks, saves a design,
                    #   restarts, and checks the panel icon keeps the design
make verify-install # stages an install, runs what it staged, builds the .deb
                    #   and checks it holds exactly the same files
```

Two things cannot be reached by unit tests, and both have shipped broken during
development: a window that opens and shows nothing, because a canvas was built
before the layout was known, and a design tab that ignores clicks. `verify-gui`
photographs the window and counts key pixels; `verify-design` clicks *Fill all*,
sets the brush to white through the real colour dialog, types a name into the real
"Save as" dialog, clicks keys and counts the new white pixels.

`verify-design` then goes further, because the features that matter most here span
processes: it closes the window and checks the design stayed on the keyboard, that
the CLI can apply the same design (which is what the systemd unit runs), that
restarting brings the design back with its white keys intact, and that *To panel*
hides the window while the program keeps running. The last step ticks *Start in
the panel*, restarts with no arguments at all — what an autostart entry does — and
asserts there is no window on screen, then that `--window` still gets one back.
Every step drives the real window and reads real pixels or real files.

Neither is a unit test: they drive the running window through X11 and AT-SPI.
`tools/gui_probe.py` is the piece that makes that possible — GTK publishes every
widget over the accessibility bus with its screen coordinates, so a test can find
the button labelled *Fill all* and click it instead of guessing at pixels. The
captures are left in `build/` so you can look at them yourself.

The canvas arithmetic itself (which key is under a click, and how close a click
has to be) lives in `layout.cpp` rather than the GUI, precisely so that it *can*
be unit tested; see the `canvas geometry` checks.

Set `CLAYMORE_GUI_TRACE=1` to have the GUI report every click, key lookup and
design write to stderr. That is how a silent no-op gets a name.

## Packaging

`make package` builds `build/claymore-linux_<version>_<arch>.deb` (the version comes
from `VERSION`) out of exactly what `make install` stages, so the two can never
install different things. It needs no `debhelper` and no `dh_make`, and the
package's `postinst` reloads the udev rules so the keyboard works without a
replug:

```sh
sudo apt install ./build/claymore-linux_1.0.0_amd64.deb
```

`make verify-install` is the check that keeps this honest: it stages an install,
runs the staged binaries, validates the desktop entry, checks that every icon is
really a PNG of the size it claims, compares the vector icon against the drawn
one, and asserts that the package contains the same files and that `uninstall`
leaves nothing behind.

The icon is one drawing with two descriptions. At run time `gui/icon.cpp` draws it
with cairo, so the window, the task bar and the panel icon are always the same
picture; `claymore-gui --export-icons <dir>` writes the PNG sizes that
`make install` puts in the icon theme, using that same drawing and needing no
display. `assets/claymore-linux.svg` is the same geometry as vectors for the
launcher, and `verify-install` fails if the two ever disagree.

## Releases

`VERSION` is the one place the version is written down; the Makefile reads it, so
`make package` names the package after it. A release is a bump, a commit and a
tag:

```sh
printf '1.1.0\n' > VERSION
git commit -am "release: 1.1.0

- The design tab keeps a design applied when the window closes
- Fixed the coloured Wave effect reverting on the AMD variant"
git tag v1.1.0
git push origin main v1.1.0
```

The release commit is what people read when they decide to upgrade, so it says
what the release brings them in a few bullets, not which files moved.

`.github/workflows/release.yml` builds that tag: it refuses a tag whose version is
not what `VERSION` says, runs the test suite, builds the `.deb` for `amd64` and
`arm64` in Ubuntu 24.04 of each architecture (arm64 under qemu), and attaches both
to the GitHub release for the tag. Running the workflow by hand does the same build
and stops at the artifacts.

## Regenerating the layout tables

`src/layout_data.cpp` is generated, not hand written:

```sh
python3 tools/gen_layout.py <openrgb>/Controllers/AsusAuraUSBController/\
AsusAuraTUFKeyboardController/AsusAuraTUFKeyboardLayouts.h > src/layout_data.cpp
```

The generator asserts that every matrix cell references a real key, that no two
keys share an LED and that the name counts match the declared LED counts, so a bad
extraction fails loudly instead of producing a subtly wrong driver.

The protocol and the per-key LED tables are derived from
[OpenRGB](https://openrgb.org/), specifically
`Controllers/AsusAuraUSBController/AsusAuraTUFKeyboardController/`, authored by
Mola19 and licensed GPL-2.0-or-later. Because this project builds on that work it
is licensed the same way: keep the derivation notes in `src/layout_data.cpp`,
`include/claymore/protocol.hpp` and `docs/PROTOCOL.md`.

## Coding agents

Read [`AGENTS.md`](AGENTS.md) and use the canonical [`docs/code-map.md`](docs/code-map.md)
before broad repository searches. Cline loads the small adapter in
`.clinerules/code-map.md`; skill-aware tools can use `skills/code-map/SKILL.md`.
The map is checked with `make verify-docs`, so update it when ownership, entry
points, commands or generated-data rules change.
