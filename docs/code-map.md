# Claymore code map

Use this before broad repository searches. It is deliberately short: locate the owning module, inspect only that module plus its public header, then make the smallest change.

## Fast commands

```sh
make                 # build all, GUI only if GTK+ 3 exists, then tests
make cli             # bin/claymore
make gui             # bin/claymore-gui
make test            # hardware-free tests/test_core.cpp
make verify-gui      # screenshot/pixel integration check; needs display
make verify-design   # drive design-tab clicks; needs display
make verify-install  # stage install and inspect files
make package         # build/rog-claymore_<version>_<arch>.deb
```

## Entry points and ownership

| Concern | Primary files | Responsibility |
| --- | --- | --- |
| CLI | `src/main.cpp` | Parse commands, compose core services, print help/errors |
| GUI lifecycle | `gui/main_gui.cpp` | GTK app, args, startup/shutdown, 30 Hz update loop |
| GUI state/actions | `gui/app.hpp/.cpp`, `gui/panels.cpp` | `AppState`, tabs, callbacks, design/settings controls |
| Tray/icon | `gui/tray.cpp`, `gui/icon.cpp` | Panel menu and runtime ROG emblem |
| Keyboard canvas | `gui/keyboard_view.cpp/.hpp` | GTK draw/events; uses core hit testing |
| Packet builders | `src/protocol.cpp`, `include/claymore/protocol.hpp` | 65-byte Aura HID reports and wire constants |
| Effects | `src/effects.cpp`, `include/claymore/effects.hpp` | Presets, defaults, speed conversion, apply sequence |
| Device/session | `src/device.cpp`, `src/hidraw.cpp` | Detection, direct mode, frame streaming, release |
| Layout | `src/layout.cpp`, generated `src/layout_data.cpp` | Variant lookup, matrix, frame geometry, hit testing |
| Colours | `src/color.cpp`, `include/claymore/color.hpp` | Parse/convert/interpolate RGB |
| Designs | `src/design.cpp`, `include/claymore/design.hpp` | Shared text parser/store, named designs, default |
| Settings | `src/settings.cpp`, `include/claymore/settings.hpp` | Config directory and key/value preferences |
| Sync | `src/sync_server.cpp`, `include/claymore/sync_server.hpp` | RGBC UNIX-datagram protocol and daemon state |
| Tests | `tests/test_core.cpp` | Core unit/regression checks; currently 391 checks |
| Generator/package | `tools/gen_layout.py`, `tools/build_deb.sh` | Reproduce layout tables; build Debian package |
| Install data | `Makefile`, `contrib/`, `udev/`, `assets/`, `docs/` | Desktop/service/udev/man/icon/package inputs |

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
- Design format/store/default selection → `design.*`; update `contrib/claymore-design.service` if startup behavior changes.
- Preference/config path → `settings.*`; keep unknown keys preserved.
- CLI command → `src/main.cpp` plus `docs/claymore.1`/README.
- GUI behavior → `gui/main_gui.cpp` first, then `app/panels`, then the relevant widget; run `make verify-gui` and `make verify-design`.
- Packaging/install metadata → `Makefile`, `tools/build_deb.sh`, `contrib/`, `udev/`, `assets/`; run `make verify-install`.

## Protocol invariants

1. Effect apply is grant → effect (and Quicksand colors) → select slot → `0x50 0x55` commit → release. Commit is mandatory.
2. Coloured Wave needs pattern bit 5 (`0x20`); colourless Wave does not.
3. Firmware speed 0 is fastest and 254 slowest. Convert user percentages with the core helpers.
4. Direct mode is a live host session. `paint` retains the final frame and does not revoke; use explicit release/effects apply to return control.
5. Claymore firmware effects persist through commit; arbitrary painted per-key designs do not. Designs are text files plus login reapplication.

## Conventions and boundaries

- Core remains libc-only. GTK 3 belongs in `gui/` and is optional for core builds.
- Use existing public headers and small helpers; do not duplicate parsers in GUI/CLI.
- `src/layout_data.cpp` is generated; edit `tools/gen_layout.py` or the source table, then regenerate.
- Preserve OpenRGB/Mola19 attribution in protocol/layout sources and GPL-2.0-or-later notices.
- Tests are hardware-free unless a script explicitly says otherwise. Do not make tests access a real keyboard.
- Keep generated/build output under `build/` and `bin/`; do not treat it as source.
