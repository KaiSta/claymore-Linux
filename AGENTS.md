# AGENTS.md

Linux C++17 control app for an ASUS ROG Claymore. Core is libc-only; the GTK 3 GUI is optional at build time. Read the compact map at `docs/code-map.md` before exploring broadly; the reusable skill is `skills/code-map/SKILL.md`.

## Commands
- `make` — CLI, GUI if GTK+ 3 is available, and tests.
- `make cli` / `make gui` — one binary only.
- `make test` / `make check` — hardware-free core tests.
- `make verify-gui` / `make verify-design` — display-dependent integration checks.
- `make verify-install` — staged install checks.
- `make package` — `.deb` via `dpkg-deb`; requires packaging tools.
- `make clean` — remove generated `build/` and `bin/` output.
- `make install PREFIX=$HOME/.local` — user-local install; `DESTDIR` stages files.

## Architecture
- `src/`, `include/claymore/`: reusable core. `src/main.cpp` is CLI composition only.
- `gui/`: GTK 3 application; GUI-only source and state live here.
- `tests/test_core.cpp`: single hardware-free test binary, currently 391 checks.
- `tools/`: generators, packaging, and verification scripts; `contrib/`, `udev/`, `docs/`, `assets/`: installation/data/docs.
- Core has no hidapi/libusb/libudev dependency: enumerate `/sys`, write `/dev/hidraw*`.
- User data is under `~/.config/claymore`; `XDG_CONFIG_HOME` and `CLAYMORE_CONFIG_DIR` are supported.

## Hard rules
- Preserve the exact firmware-effect sequence in `src/effects.cpp`: grant, effect/quicksand, select slot, **commit `0x50 0x55`**, release. Omitting commit causes a temporary flash that reverts.
- `protocol::effect()` must retain the derived bit-5 pattern/colour rule for coloured Wave; colourless Wave keeps it clear. Keep byte-level packet tests in sync.
- Firmware speed `0` is fastest; use `speed_to_percent()` for the user-facing scale and existing per-effect defaults.
- Direct mode is a host-driven session. Use `paint` for a single process/session; a separate CLI invocation enters/leaves the wrong lifecycle. `paint` intentionally does not revoke; release explicitly via `claymore release`, GUI hand-back, or an Effects apply.
- Claymore cannot save arbitrary per-key designs. Do not promise flash persistence; use named text designs and the login helper service.
- `src/layout_data.cpp` is generated from OpenRGB by `tools/gen_layout.py`; edit the generator/source data, not the generated table by hand.
- Protocol and layout tables are derived from OpenRGB/Mola19; retain GPL-2.0-or-later attribution.

## Conventions
- Match existing C++17 style, include order, naming, and small pure helpers; avoid new dependencies unless necessary.
- Keep protocol bytes and wire formats centralized in `src/protocol.cpp` / `src/sync_server.cpp`.
- Keep shared parsers in core; do not duplicate design/settings parsing in GUI.
- Add or update `tests/test_core.cpp` for core behavior. GUI work should also run the relevant `make verify-*` target when a display is available.
- Keep user-visible CLI/man/desktop documentation aligned when behavior changes.
- Do not commit, push, reset, or overwrite unrelated work.

## Change index
- Protocol/packets: `src/protocol.cpp`, `include/claymore/protocol.hpp`, `docs/PROTOCOL.md`.
- Effects/defaults/apply: `src/effects.cpp`, `include/claymore/effects.hpp`, `src/main.cpp`, `gui/panels.cpp`.
- Device/session: `src/device.cpp`, `src/hidraw.cpp`, corresponding headers.
- Layout/keys: `src/layout.cpp`, generated `src/layout_data.cpp`, `tools/gen_layout.py`.
- Designs: `src/design.cpp`, `include/claymore/design.hpp`, `contrib/claymore-design.service`.
- Settings/config: `src/settings.cpp`, `include/claymore/settings.hpp`.
- Sync: `src/sync_server.cpp`, `include/claymore/sync_server.hpp`.
- GUI lifecycle/widgets: `gui/main_gui.cpp`, `gui/app.*`, `gui/panels.cpp`, `gui/tray.cpp`, `gui/keyboard_view.*`, `gui/icon.*`.
- Packaging/install: `Makefile`, `tools/build_deb.sh`, `contrib/`, `udev/`, `assets/`.

## Agent compatibility
`AGENTS.md` is intentionally plain Markdown and is discoverable by many agents. Cline users can enable the same guidance through `.clinerules/code-map.md`; other tools may use `AGENTS.md` directly or copy/symlink it to their preferred instruction file. The portable skill at `skills/code-map/SKILL.md` has standard YAML front matter and no tool-specific syntax.
