# Claymore code map

Canonical compact map for agents. Read `AGENTS.md` first. Locate the owning module, inspect only that module plus its public header and related tests, then make the smallest change.

## Fast commands

```sh
make                 # build all, GUI if GTK+ 3 exists, then tests
make cli             # bin/claymore
make gui             # bin/claymore-gui
make test            # hardware-free tests/test_core.cpp
make verify-docs     # validate this map and agent guidance
make verify-gui      # GUI pixel check; display and test tooling required
make verify-design   # design-tab interaction check; display and AT-SPI required
make verify-install  # staged install/package check
make package         # build/claymore-linux_<version>_<arch>.deb
```

## Entry points and ownership

| Concern | Primary files | Responsibility |
| --- | --- | --- |
| CLI | `src/main.cpp` | Parse commands, compose core services, print help/errors |
| GUI lifecycle | `gui/main_gui.cpp` | GTK app, args, startup/shutdown, update loop |
| GUI state/actions | `gui/app.hpp`, `gui/app.cpp`, `gui/panels.cpp` | `AppState`, tabs, callbacks, design/settings controls |
| Tray/icon | `gui/tray.cpp`, `gui/icon.cpp` | Panel menu and runtime ROG emblem |
| Keyboard canvas | `gui/keyboard_view.hpp`, `gui/keyboard_view.cpp` | GTK draw/events using core hit testing |
| Packets | `src/protocol.cpp`, `include/claymore/protocol.hpp` | 65-byte Aura HID reports and constants |
| Effects | `src/effects.cpp`, `include/claymore/effects.hpp` | Presets, defaults, speed conversion, apply sequence |
| Device/session | `src/device.cpp`, `src/hidraw.cpp` | Detection, direct mode, streaming, release |
| Layout | `src/layout.cpp`, generated `src/layout_data.cpp` | Variants, matrix, frame geometry, hit testing |
| Colours | `src/color.cpp`, `include/claymore/color.hpp` | RGB parsing, conversion, interpolation |
| Designs | `src/design.cpp`, `include/claymore/design.hpp` | Text parser/store, named designs, default |
| Settings | `src/settings.cpp`, `include/claymore/settings.hpp` | Config directory and key/value preferences |
| Sync | `src/sync_server.cpp`, `include/claymore/sync_server.hpp` | RGBC UNIX-datagram protocol and daemon state |
| Tests | `tests/test_core.cpp` | Hardware-free regression suite |
| Generator/package | `tools/gen_layout.py`, `tools/build_deb.sh` | Reproduce layout tables; build Debian package |
| Install data | `Makefile`, `VERSION`, `contrib/`, `udev/`, `assets/`, `docs/` | Desktop/service/udev/man/icon/package inputs, release version |

## Runtime/data flow

```text
CLI main ─┐
          ├─ Device ── Hidraw (/sys discovery, /dev/hidraw*)
          ├─ Protocol packets ── ASUS firmware
          ├─ Effects/Layout/Design/Settings
          └─ SyncServer ── UNIX datagram socket

GUI main ── AppState ── panels/widgets ── core modules
              └─ tray/icon ── GTK/cairo
```

`Frame` slots are layout order; `KeyName::index` is the device LED index. Do not interchange them. Direct mode maps the former to the latter.

## Where to change what

- Packet/constant/sequence bug → `protocol.*`, then `docs/PROTOCOL.md` and packet tests.
- New preset or speed default → `effects.*`; add regression coverage in `tests/test_core.cpp`.
- Device discovery/read/write/session lifecycle → `device.*`, `hidraw.*`.
- Key map, region, numpad, canvas hit testing → `layout.*`; if generated tables change, use `tools/gen_layout.py`.
- Design format/store/default → `design.*`; update `contrib/claymore-design.service` if startup behavior changes.
- Preference/config path → `settings.*`; preserve unknown keys.
- CLI command → `src/main.cpp` plus `docs/claymore.1` and `README.md`.
- GUI behavior → `gui/main_gui.cpp` first, then `app/panels`, then the relevant widget; run `make verify-gui` and `make verify-design`.
- Packaging/install metadata → `Makefile`, `tools/build_deb.sh`, `contrib/`, `udev/`, `assets/`; run `make verify-install`. The developer walkthrough (build, checks, packaging, releases, generated tables) is `CONTRIBUTING.md`.
- Release version or tag rules → `VERSION`, `.github/workflows/release.yml`, `.github/ci/build-deb.sh`; the workflow rejects a tag that disagrees with `VERSION`.
- Agent guidance/map changes → `AGENTS.md`, `skills/code-map/SKILL.md`, `.clinerules/code-map.md`; run `make verify-docs`.

## Protocol invariants

1. Effect apply is grant → effect (and Quicksand colours) → select slot → `0x50 0x55` commit → release. Commit is mandatory.
2. Coloured Wave needs pattern bit 5 (`0x20`); colourless Wave does not.
3. Firmware speed 0 is fastest and 254 slowest. Convert user percentages with the core helpers.
4. Direct mode is a live host session. `paint` retains the final frame and does not revoke; use explicit release/effects apply to return control.
5. Claymore firmware effects persist through commit; arbitrary painted per-key designs do not. Designs are text files plus login reapplication.

## Generated and external data

- Do not hand-edit `src/layout_data.cpp`. Regenerate it from the OpenRGB header:

  ```sh
  python3 tools/gen_layout.py <openrgb>/Controllers/AsusAuraUSBController/\\
  AsusAuraTUFKeyboardController/AsusAuraTUFKeyboardLayouts.h > src/layout_data.cpp
  ```

- `gui/icon.cpp` and `assets/claymore-linux.svg` intentionally describe the same emblem; `make verify-install` compares them.
- `build/` and `bin/` are generated and ignored. Build packages with `make package` when a release artifact is needed; do not stage generated output.
- Protocol/layout data is derived from OpenRGB/Mola19; retain GPL-2.0-or-later attribution.

## Boundaries and response discipline

- Core remains libc-only. GTK 3 belongs in `gui/` and is optional for core builds.
- Use existing public headers and small helpers; do not duplicate parsers in GUI/CLI.
- Tests are hardware-free unless a script explicitly says otherwise; never make unit tests access a real keyboard.
- Direct-mode callers that should return control on teardown must opt into `set_release_on_destroy(true)`; the GUI design mode intentionally leaves the final frame retained.
- For a change, report only files changed, behavior changed, commands run, and unresolved risks.
