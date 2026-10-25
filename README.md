# Claymore@Linux: RGB lighting control for the ASUS ROG Claymore keyboard

Claymore@Linux controls every LED on an **ASUS ROG Claymore** mechanical
keyboard from **Linux**: firmware effects, per-key RGB designs, an animated GTK window and an Aura Sync
style streaming socket. No Armoury Crate, no Windows, no vendor software, no
daemon left running.

Two programs are built from this repository:

| Program | What it is |
| --- | --- |
| `claymore` | Command line tool: firmware effects, per-key painting, terminal preview and the sync daemon. |
| `claymore-gui` | GTK 3 application: live keyboard preview, effect gallery, per-key design editor, panel icon and sync server panel. |

![The Claymore@Linux GUI showing the effects tab with an animated preview of the ROG Claymore keyboard](docs/screenshot.png)

**Read on:** [Features](#features) · [Requirements](#requirements) ·
[Install](#install) · [The GUI](#the-gui) · [Command line](#command-line) ·
[Everyday recipes](#everyday-recipes) · [Effects](#firmware-effects) ·
[Layouts](#keyboard-layouts) · [Sync / Aura Sync](#sync-daemon-aura-sync-on-linux) ·
[Troubleshooting](#troubleshooting-and-faq) · [Limitations](#known-limitations) ·
[Contributing](#contributing) · [Licence](#licence-and-attribution)

## Features

* **Firmware effects, animated by the keyboard itself** — static, breathing,
  spectrum cycle, reactive, wave, ripple, starry night, quicksand, current and
  rain drop. Because the Claymore runs them, they cost no host CPU and can be
  written to flash so they survive a reboot.
* **Direction, speed and colour per effect**, including random colour modes where
  the firmware supports them, with each effect defaulting to the speed ASUS ships
  for it.
* **Per-key RGB painting** — paint any key a different colour in the GUI or with
  one CLI command, at up to 30 frames per second, and let the board keep the
  last frame after the program exits.
* **Named designs** — save, load, delete and mark one as the startup design, so
  the same picture comes back at login through a systemd user unit (nothing left
  running) or when the GUI starts.
* **A panel icon** so the window can be put away while the design stays applied,
  with a menu to reopen it, re-apply the design, hand the keyboard back to its
  firmware, or quit and keep the picture.
* **Aura Sync on Linux** — the sync daemon streams frames from any program to the
  keyboard over a UNIX datagram socket, so your own scripts, visualisers or music
  tools can share the backlight.
* **Both legend sets and the detachable numpad** on either side, detected
  automatically and overridable per command.
* **True colour terminal preview** of any effect, with no keyboard attached, for
  trying things out over SSH or before buying the board.
* **No dependencies beyond libc** for the driver and CLI — no `hidapi`, no
  `libusb`, no `libudev`; GTK 3 is needed only for the GUI. The package is a
  single `.deb` for `amd64` or `arm64`.

## Requirements

* Linux with `hidraw` support (`/dev/hidraw*` and `/sys`), which every mainstream
  distribution has.
* A C++17 compiler to build it, and GTK 3 development headers if you want the
  GUI. Everything else builds without GTK.

The driver reads `/sys` and writes `/dev/hidraw*` directly (`src/hidraw.cpp`), so
there is no dependency on `hidapi`, `libusb` or `libudev`.

## Install

### Ubuntu and Debian: install the .deb

Pick `claymore-linux_<version>_amd64.deb` or `claymore-linux_<version>_arm64.deb`
from the release page and install it. The package is called `claymore-linux`,
because an `@` cannot appear in a Debian package name; the commands it installs
are `claymore` and `claymore-gui`.

```sh
sudo apt install ./claymore-linux_1.0.0_amd64.deb
```

The package contains both programs, the man pages, the desktop entry, the icons,
the udev rule and the systemd user unit, and its `postinst` reloads the udev
rules so the keyboard works without a replug. It depends on `libc6 (>= 2.34)`
and GTK 3 (`libgtk-3-0t64` or `libgtk-3-0`, whichever your release calls it).

### Any distribution: build and install from source

```sh
make                               # CLI, GUI if GTK 3 is present, and the tests
sudo make install                  # into /usr/local
PREFIX=$HOME/.local make install   # or without root
sudo make uninstall                # your designs in ~/.config/claymore are kept
```

`make install` puts the two binaries, the man pages, the desktop entry, the udev
rule, the systemd user unit and the icons where they belong, and tells you if
GTK 3 was missing, because then there is no GUI and no icons.

To uninstall a `.deb`: `sudo apt remove claymore-linux`. Your designs in
`~/.config/claymore` are never touched.

### Give your user access to the keyboard

Reading and writing the keyboard needs access to `/dev/hidraw6` (the Aura control
interface). `sudo make install` and the `.deb` install the udev rule for you; by
hand it is:

```sh
sudo cp udev/99-claymore-linux.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Then replug the keyboard, or log out and back in. The rule grants the `plugdev`
group access; adjust `GROUP=` in the rule if your distribution uses something
else.

Without the rule, `claymore list` still works because it only reads, but anything
that changes the lighting reports a permission error.

### Check that it found the keyboard

```sh
claymore list      # what hidraw nodes exist, and which is the Claymore
claymore info      # firmware version, numpad side, legend set, active layout
```

## The GUI

```sh
claymore-gui
```

Three tabs:

* **Effects** — the preset gallery on the left with an animated preview of the
  keyboard above the controls. Speed, brightness, direction, random colours and
  the colour swatches all apply to the selected effect. *Live apply* pushes
  changes as you drag. There is no "don't save" option, because the commit packet
  is what makes an effect appear at all; see
  [Firmware effects](#firmware-effects).
* **Design your own** — paint keys with the brush, use the eyedropper to copy a
  key's colour, middle click to erase, or start from *Rainbow fill*. Designs are
  saved under a name in `~/.config/claymore/designs/`, and the picker on the tab
  loads, saves, deletes and switches between them. *Load at startup* marks one to
  be loaded whenever the GUI starts, and it is the same design
  `claymore paint --default` applies at login.

  Each design is a plain text file, one `slot colour` per line, so designs are
  diffable, hand editable and scriptable.

  Your first edit shows the design on the keyboard by itself, so painting a key
  lights that key, and it **stays there when you close the window**, because the
  keyboard keeps the last frame it is given. Streamed frames stay out of the way
  until that first edit: holding the keyboard in direct mode pauses the firmware
  effect, so claiming the board the moment the tab opens would make the *Effects*
  tab look broken. Untick *Show on keyboard* to hand the board back, and read
  [how to keep a design on the keyboard](#keep-a-design-on-the-keyboard) for the
  details and for surviving a reboot.

  The canvas title tracks the tool as you switch — *Click or drag to paint*,
  *Click or drag to erase*, *Click a key to copy its colour* — because the
  eyedropper deliberately does not paint and used to look like a canvas that
  ignored clicks.
* **Sync** — start and stop the sync server and watch incoming colours appear.

The window carries the ROG eye emblem as its icon, drawn with cairo at runtime
rather than loaded from a file, so the title bar, the task list and the panel icon
are all the same drawing. `assets/claymore-linux.svg` is the same emblem as vectors
for the launcher, and `make install` also puts PNGs of it in the icon theme.

### Put the window in the panel

*To panel* in the header hides the window and leaves a ROG icon in the panel with
the program still running. The design stays applied, and anything that needs to
keep ticking — a preview, or a design with movement in it — keeps ticking.

The icon's menu offers *Show the window*, *Re-apply my design* (useful after
something else grabbed the lighting), *Hand the keyboard back*, and *Quit and keep
the design*.

### Start in the panel, and start at login

*Start in the panel* in the header (or `claymore settings start-in-tray on`) saves
the choice, so from then on the window stays away and only the panel icon
appears. That is the setting to turn on **before** adding the program to your
desktop's autostart, and it is why the autostart entry needs no arguments:

```sh
claymore settings start-in-tray on
install -Dm644 contrib/autostart/org.claymore.linux.desktop \
    ~/.config/autostart/org.claymore.linux.desktop
```

One-off flags:

```sh
claymore-gui --tray        # hide the window for this run, whatever the setting says
claymore-gui --window      # show it for this run even though the setting is on
claymore-gui --no-design   # ignore the startup design for this run
claymore settings          # list the preferences, and where they live
```

`make install` deliberately does not install the autostart file: starting a
program at everybody's login is the user's decision, not the package's. The
systemd unit below is the other option, for a design applied at login with nothing
left running at all.

If the desktop has no tray (GNOME without an extension, for instance), the window
stays open and says so, rather than leaving a running program that cannot be seen
or reached.

The previews are software renderings of the effects; the keyboard does the real
animating for firmware effects. That distinction matters for the reactive and
ripple effects, whose previews are approximations.

## Command line

```sh
claymore list                    # what hidraw nodes exist, and which is the Claymore
claymore info                    # firmware, numpad side, legend set, active layout
claymore keys                    # every key: frame slot, device LED index, name
claymore layout                  # ASCII map of the matrix

claymore static rog-red          # one solid colour
claymore off                     # black
claymore effect breathing --color ff003c --color 140024 --speed 45
claymore effect wave --color ff003c --color 00c8ff --direction left --speed 40
claymore effect color-cycle --speed 60
claymore preset rainbow-wave

claymore paint --all 300000 --key escape=00ff00 --key numpad-lock=ff00ff
claymore paint --design claymore-design.txt
claymore release                 # hand the keyboard back to its firmware
claymore reset                   # release, then apply a firmware effect

claymore preview wave            # animate an effect in the terminal
claymore preview quicksand --speed 30

claymore designs                 # saved designs, marking the startup one
claymore default Desk            # apply 'Desk' at login, or 'none'
claymore settings                # the preferences shared with the GUI
claymore settings start-in-tray on
```

**Every effect change is stored in the keyboard.** The Claymore will not display
an effect until it has been committed with the `0x50/0x55` packet, and that same
packet is what writes flash. There is no way to preview an effect without also
storing it, so `--no-save` exists only for compatibility and prints a note that it
is ignored. `docs/PROTOCOL.md` documents the packet sequence.

Each effect defaults to the speed the vendor software ships for it, so
`claymore effect wave --color ff0000 --color 0000ff` looks right without tuning.
The `0..100` speed scale is linear in the firmware's byte and `0` is the
*fastest*, so the top of the range is genuinely extreme.

Key names come from the layout, so `claymore keys` is the reference for what you
can pass to `--key`. Slugs (`left-shift`, `f5`) and raw device LED values
(`0x45`) both work.

### Why `paint` exists, and why `fill` + `set` is a trap

Firmware effects are animated by the keyboard. Direct mode is the opposite: the
host takes responsibility for every LED, and granting it pauses the firmware, so
the board shows only what the host sends.

**Entering direct mode clears the board.** Each `claymore` invocation is a new
process that grants direct mode from scratch, so this

```sh
claymore fill 300000        # board goes dim red
claymore set escape 00ff00  # this blanks the board, then lights Escape
```

leaves you with a single lit key, not a red board with a green Escape. Use one
invocation, which composes a single frame:

```sh
claymore paint --all 300000 --key escape=00ff00
```

If a direct mode session ever leaves the board dark, the fix is one command:

```sh
claymore release
```

`set` and `fill` still exist and still work; they just warn that they repaint the
whole board. The sync daemon releases remote control when it exits, so it cannot
leave you in the dark.

## Everyday recipes

### Keep a design on the keyboard

A painted design is not an effect, and that distinction decides how long it
lasts.

**The keyboard's flash cannot hold one.** It stores its built-in effects,
parameterised by a handful of colours; the `0x50/0x55` commit packet that stores
an effect has no counterpart for a per-key picture. OpenRGB marks its
`DeviceSaveMode` "not available for Claymore" for the same reason. This is a
hardware limit, not a gap in the project, and it is why Aura Creator on Windows
needs its own service running to light per-key layouts.

**But the board does keep the last frame it is given**, so a design needs no
process at all to stay lit:

* `claymore paint --design ~/.config/claymore/design.txt` puts a design on the
  board and deliberately does *not* revoke remote control. The keyboard keeps
  showing it after the command exits.
* The GUI does the same on exit whenever a design is on the keyboard, and saves
  the design to that file on the way out. Closing the window no longer throws the
  design away.
* `claymore release` hands the keyboard back to its own effects, and unticking
  *Show on keyboard* in the GUI does the same thing.

The trade is that a keyboard holding a design is in remote control: its own
effects, including the Fn key lighting modes, are paused until you release it.
Applying any effect from the *Effects* tab, or the `effect` command, takes the
board back too, so nothing can get permanently stuck.

**Across a reboot** the design has to be re-applied, because the frame lives in
RAM. `contrib/claymore-design.service` is a systemd user unit that does exactly
that:

```sh
mkdir -p ~/.config/systemd/user ~/.local/bin
cp contrib/claymore-design.service ~/.config/systemd/user/
# edit ExecStart in that file if claymore is not in ~/.local/bin
systemctl --user enable --now claymore-design.service
```

It is a one shot, not a daemon: it runs `claymore paint --default`, which loads
whichever design you marked as the startup one, retrying for up to 30 seconds
while the hidraw node appears, then exits. Nothing stays running, and the lighting
does not depend on this project being resident. Change a design in the GUI and the
unit picks up the new one at the next login.

The GUI and the unit load the same design, so the two agree by construction rather
than by two files kept in step:

```sh
claymore designs              # what you have, and which one starts
claymore default Desk         # choose one, or 'none' to clear it
claymore paint --default      # what the unit runs
```

### Preview an effect in the terminal

`claymore preview <effect>` renders the effect with true colour ANSI blocks using
the same software renderer as the GUI. It needs no keyboard attached, which makes
it the quickest way to see what an effect looks like:

```sh
claymore preview wave --speed 40
```

## Firmware effects

| Name | Firmware mode | Colours | Notes |
| --- | --- | --- | --- |
| `static` | 0 | 1 | |
| `breathing` | 1 | 1-2 | pulses between two colours |
| `color-cycle` | 2 | 0 | sweeps the hue wheel |
| `reactive` | 3 | 1-2 | keys flare under your fingers, random colours supported |
| `wave` | 4 | 0-7 | 0 colours means the firmware rainbow wave; with colours the firmware gets the pattern bit automatically |
| `ripple` | 5 | 1-8 | rings expand from each key press |
| `starry-night` | 6 | 1-3 | sparse twinkling |
| `quicksand` | 7 | 6 | exactly six colours, sent in a separate packet |
| `current` | 8 | 1-3 | drifting bands |
| `rain-drop` | 9 | 1-3 | droplets falling down |

Speed runs 0-100 in the interface and is converted to the firmware's 0-254 range,
where a higher firmware value is slower.

## Keyboard layouts

The keyboard does not reliably report which legends it has, so the tool infers it
from the USB `bcdDevice` version and reads the numpad position from the keyboard
itself. Both can be overridden:

```sh
claymore info --region uk       # force ISO key legends
claymore keys --region us
claymore info --numpad left
claymore layout --layout numpad-left-uk
claymore layout --list          # all six variants
```

If `claymore info` reports the wrong legend set, pass `--region` to the command
you are running. Only key names are affected, not which LED lights up.

## Sync daemon: Aura Sync on Linux

The sync daemon holds the keyboard in direct mode while any process pushes frames
to it over a UNIX datagram socket — the closest thing to Aura Sync on Linux, and
the way to drive the keyboard from your own scripts.

```sh
claymore syncd --fps 30
```

```
claymore: streaming frames on /run/user/1000/claymore-linux.sock (88 slots, 30 fps)
```

| Option | Meaning |
| --- | --- |
| `--socket <path>` | socket to listen on, default `$XDG_RUNTIME_DIR/claymore-linux.sock` |
| `--fps <n>` | frames per second, default 30 |
| `--demo` | skip the socket and animate a rainbow internally |
| `--stdin` | read frames from stdin, one hex or `RGBC` message per line |
| `--idle-effect <name>` | firmware effect to restore when senders go quiet |
| `--idle-timeout <ms>` | how long without frames before restoring it, default 1500 |

The wire format is documented in `docs/PROTOCOL.md`. A minimal client:

```python
import os, socket

KEY_COUNT = 88
sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
path = os.environ.get("XDG_RUNTIME_DIR", "/tmp") + "/claymore-linux.sock"

frame = b"RGBC" + bytes([1, 0, KEY_COUNT & 0xFF, KEY_COUNT >> 8])
frame += bytes([0xFF, 0x00, 0x3C] * KEY_COUNT)
sock.sendto(frame, path)
```

Sparse updates only need the keys that changed: set the sparse flag and send
`slot, r, g, b` entries. Sending a `release` message hands the keyboard back to its
firmware.

Without a running daemon, `claymore syncd --demo` confirms the direct mode
pipeline works.

## Troubleshooting and FAQ

### Is there an Armoury Crate for Linux?

No. ASUS ships no Linux software for the Claymore, so this project talks to the
keyboard's own USB HID protocol directly. `docs/PROTOCOL.md` records the packet
reference, where it came from, and the two undocumented quirks the driver depends
on.

### "no hidraw device matching 0b05:184d"

The keyboard is not plugged in, or `/sys` is unavailable in a container. Check
`claymore list`.

### "Permission denied"

The udev rule is not installed. See
[Give your user access to the keyboard](#give-your-user-access-to-the-keyboard).

### The keyboard's LEDs stay dark, or my script left it stuck

The board is still in direct mode. Run `claymore release` to hand it back to its
firmware, or unplug and replug the keyboard.

### Effects do not survive a reboot

An effect is only stored in flash once it has been committed: pass `--persist`, or
tick *Save to flash* in the GUI. Saving is off by default in the GUI because the
Claymore rewrites flash on every save.

A **per-key design** never survives a reboot, because flash cannot store one; see
[keep a design on the keyboard](#keep-a-design-on-the-keyboard) for the systemd
unit that re-applies it at login.

### `claymore info` reports the wrong key legends

Override it with `--region uk` (or your region) on the command you are running.

### Does this work with other ASUS keyboards?

It is written for the Claymore (`0b05:184d`) and has only been verified on that
one. Other Aura keyboards share parts of the protocol, but their layouts and effect
sets differ, so nothing else is supported.

### Can another program drive the keyboard?

Yes — through the [sync daemon](#sync-daemon-aura-sync-on-linux). It owns the
keyboard and accepts frames over a socket, so a visualiser, a music player plugin
or your own script does not have to fight the CLI for the device.

## Known limitations

* **Verified against one keyboard**, the author's own ANSI unit with firmware
  `01.09.01`, numpad mounted left and ISO legends. `info`, `keys`, `static`,
  `breathing`, `paint` and `release` were confirmed on real hardware and the
  detected key map was confirmed correct, but exotic firmware revisions may
  behave differently.
* The four step **brightness** control is an extrapolation from an undocumented
  byte and is labelled experimental in the interface.
* **Key press events are not exposed**; the reactive and ripple effects are driven
  by the firmware, and their software previews are approximations because the
  keyboard cannot report its LED colours back.
* Per-key **animations** are not exposed in the GUI yet, only static designs.
* A per-key design **cannot be stored in the keyboard** — a hardware limit, not a
  missing feature, explained
  [above](#keep-a-design-on-the-keyboard).

## Contributing

Patches, bug reports and reports from other Claymore firmware revisions are
welcome. Everything about building, testing, packaging, releasing and
regenerating the layout tables is in [`CONTRIBUTING.md`](CONTRIBUTING.md):

```sh
make            # CLI + GUI + tests
make test       # hardware-free test suite
make package    # build/claymore-linux_<version>_<arch>.deb
```

Coding agents should start with [`AGENTS.md`](AGENTS.md) and the canonical
[`docs/code-map.md`](docs/code-map.md).

## Licence and attribution

GPL-2.0-or-later. See [`LICENSE`](LICENSE).

The protocol and the per-key LED tables are derived from
[OpenRGB](https://openrgb.org/), specifically
`Controllers/AsusAuraUSBController/AsusAuraTUFKeyboardController/`, authored by
Mola19 and licensed GPL-2.0-or-later. Because this project builds on that work it
is licensed the same way, and the derivation is recorded in `src/layout_data.cpp`,
`include/claymore/protocol.hpp` and `docs/PROTOCOL.md`.
