# ASUS ROG Claymore lighting protocol

This documents the USB HID protocol this tool drives. It is written down so the
work can be audited and extended.

## Where this came from

The wire format was **reverse engineered by the OpenRGB project**, in
`Controllers/AsusAuraUSBController/AsusAuraTUFKeyboardController/`
(authored by Mola19, GPL-2.0-or-later), which in turn descends from packet
captures of ASUS' own Windows tooling. This project re-implements that protocol
in C++ for Linux and does not claim to have discovered it.

What *was* verified independently here: the per-key LED tables were extracted
mechanically from the OpenRGB source by `tools/gen_layout.py` rather than
transcribed, and every packet builder has byte-level unit tests in
`tests/test_core.cpp`.

## Device identity

| Property | Value |
| --- | --- |
| USB vendor | `0x0B05` (ASUSTeK) |
| USB product | `0x184D` (ROG Claymore) |
| HID interface | `1` |
| HID usage page | `0xFF00` |
| Report length | 65 bytes |

The Claymore exposes three or more HID collections. Only interface 1 carries the
Aura control collection, which is why the tool filters on the interface number
(and, when it can read the report descriptor, the usage page).

## Report format

Every host→device report is 65 bytes:

```
byte 0      report number, 0x00
byte 1..    command payload
```

The Claymore's Aura collection uses numbered reports, so byte 0 is a real report
ID of zero. A device that does not use numbered reports wants the payload alone;
the transport detects this by retrying a rejected 65 byte write as 64 bytes.

Device→host reports are read as 65 bytes with the same report ID prefix, so a
reply byte at index *n* is at the same offset regardless of how the kernel
delivered it.

## Handshake

Remote control is a permission the firmware grants the host. It must be held for
the duration of an effect change and released afterwards:

```
0x41 0x01   grant remote control
0x41 0x03   grant exclusive remote control (direct mode)
0x41 0x00   release remote control
```

Sequence for a firmware effect, in this exact order:

```
grant           0x41 0x01
effect packet   see below
[quicksand]     0x51 0x91 + six colours
select slot     0x51 0x28 0x00 0x00 <mode>
commit          0x50 0x55        (flash write, ~50 ms to acknowledge)
release         0x41 0x00
```

### The commit packet is not optional

The `0x50 0x55` packet is easy to mistake for "write this to flash", and it
would be reasonable to skip it when a caller does not want a persistent effect.
That is wrong, and it was measured on real hardware:

> Skip the commit and the board flashes the new effect for roughly a second,
then reverts to the previously committed one. The effect looks like it was
applied and then immediately undone.

So the commit is what makes the effect take effect at all. There is no way to
preview an effect on this keyboard without also storing it. The reference
implementation always sends it for the Claymore, which in hindsight was the
hint.

Corollary: `release` (`0x41 0x00`) is where an uncommitted effect gets thrown
away, so a sequence that grants control and never releases leaves the board
dark rather than showing the effect.

Sequence for direct mode:

```
grant           0x41 0x01
grant direct    0x41 0x03
per frame       0xC0 0x81 ...    (see "Direct mode")
```

## Firmware effect packet

```
0x51 0x2C
byte  3   0x00
byte  4   0x00
byte  5   mode
byte  6   speed
byte  7   flags
byte  8   0xFF
byte  9   0xFF
byte 10   light level
byte 11.. colours, three bytes each
```

`mode` values:

| Value | Effect |
| --- | --- |
| 0 | Static |
| 1 | Breathing |
| 2 | Spectrum cycle |
| 3 | Reactive |
| 4 | Wave |
| 5 | Ripple |
| 6 | Starry night |
| 7 | Quicksand |
| 8 | Current |
| 9 | Rain drop |
| 15 | Direct |

`speed` runs 0..254, where **higher is slower**. 0 is the fastest setting and
254 the slowest. The defaults the vendor software ships with are 107 for
breathing, 121 for spectrum cycle, 56 for reactive, 50 for wave, 108 for ripple,
54 for starry night and 103 for quicksand. This tool converts them to its own
0..100 scale with `speed_to_percent()` and uses them as the per-effect defaults,
so nothing is jarringly fast out of the box. Because the mapping is linear,
90 percent already sits near the firmware's maximum.

`flags` is a bitfield:

```
bit 7 (0x80)  random colours
bit 5 (0x20)  patterned / colour wave
bit 2..0      direction: left 0, right 4, up 6, down 2
```

### A coloured wave needs bit 5

This one is easy to get wrong and hard to diagnose from the outside. For mode 4
(Wave) with explicit colours, bit 5 **must** be set. Without it the firmware
runs its own rainbow and largely ignores the palette you sent, which looks like
the colours being wrong rather than being absent.

The reference implementation arrives at the same bit by rewriting colour mode
`specific` as `pattern` for Wave only. Observed on hardware, a cyan and red wave
sent with bit 5 clear produced cyan and blue with no red anywhere; with bit 5
set, the same packet produced a red board with a blue travelling crest, which is
exactly the two colours that were asked for.

A colourless Wave is the vendor rainbow and must have bit 5 **clear**.
Random (`bit 7`) and pattern (`bit 5`) are mutually exclusive.

### How a colour wave looks

The gradient is swept in device LED index space, not in physical key position.
On the ANSI no-numpad board consecutive indices 56..60 happen to be one physical
column, so the wave reads as a vertical band of the second colour travelling
across a background of the first. That is correct behaviour, not a mapping bug.

`light level` (byte 10) is not a general brightness. The reference
implementation sends `0xFF` for every effect except spectrum cycle, which gets
`0x80`. This tool maps its four step brightness control onto
`0x00 / 0x55 / 0xAA / 0xFF` and labels that **experimental**, because it is an
extrapolation rather than a documented behaviour.

Bytes 17, 29, 41 and 53 are stamped `0xFF`. Their meaning is unknown; the
firmware expects them and the reference implementation always sets them.

### Quicksand

Quicksand carries no colours in the effect packet at all. It takes exactly six
colours from a second packet:

```
0x51 0x91
byte 5..   six colours, three bytes each
```

## Direct mode

When the host is in charge of every LED, reports of this shape are streamed:

```
0xC0 0x81 <count> 0x00
then <count> entries of: <led index> <r> <g> <b>
```

`count` is at most 15, so a full 106 LED keyboard needs eight reports per frame.

A single LED can be addressed with:

```
0xC0 0x81 0x01 0x00 <led index> <r> <g> <b>
```

The LED index is the *device* LED value, not the key's position in this tool's
frame array. The two are different numbering schemes; see below.

## Queries

| Purpose | Request | Reply |
| --- | --- | --- |
| Firmware version | `0x10 0x01 0x00 0x00 0x00`, then `0x12 0x22` | ASCII hex from bytes 8, 9, 10 as `XX.XX.XX` |
| Numpad position | `0x40 0x60` | `byte[5] * 2 + byte[4]`, where 0 = absent, 2 = right, 3 = left |

Flash save: `0x50 0x55`.

## Key legends

Which physical legends the board has (ANSI/US or ISO/UK) is not reported by any
command. The reference implementation infers it from the USB `bcdDevice`
version, using the high nibble:

| `bcdDevice >> 12` | Layout |
| --- | --- |
| 2 or 3 | ISO ("UK") |
| anything else | ANSI ("US") |

That is a heuristic, so `--region us|uk` exists to override it when it guesses
wrong. Getting it wrong shifts the key names but not the LED indices.

## LED numbering, and why there are two

There are two numbering schemes in play and mixing them up is the easiest way to
produce a driver that lights the wrong keys:

* **Device LED value** — the byte in a direct mode packet. Sparse, and not
  contiguous. On an ANSI board without the numpad it runs from `0x00` to `0x95`
  while only 88 LEDs exist.
* **Frame slot** — this tool's dense index, 0..LED count-1, in the order the key
  names appear in the layout table. A frame is one colour per frame slot, so a
  dense frame is `3 * led_count` bytes rather than `3 * 255`.

`claymore keys` prints both for every key. `claymore layout` shows the matrix and
which frame slot sits where.

## The layout matrix

Each variant stores a `rows × cols` matrix whose cells hold frame slots, or `-1`
for empty space. Rows are ordered top to bottom and columns left to right. A cell
appearing twice would mean two physical keys sharing one LED, which the test
suite asserts never happens.

Six variants exist, covering the detachable numpad on either side and both legend
sets:

| Variant | LEDs | Matrix |
| --- | --- | --- |
| `no-numpad-us` | 88 | 7 × 19 |
| `no-numpad-uk` | 89 | 7 × 19 |
| `numpad-right-us` | 105 | 7 × 24 |
| `numpad-right-uk` | 106 | 7 × 24 |
| `numpad-left-us` | 105 | 7 × 24 |
| `numpad-left-uk` | 106 | 7 × 24 |

## The sync wire format

Not part of the ASUS protocol. This is this project's own interface, modelled on
what Aura Sync offers on Windows: a daemon holds the keyboard in direct mode and
any process may push frames to it.

One UNIX datagram per frame, little endian:

```
offset 0   4 bytes  magic "RGBC"
offset 4   1 byte   version, currently 1
offset 5   1 byte   flags: bit 0 sparse, bit 1 release
offset 6   2 bytes  entry count
offset 8   payload
```

* Dense payload: `count * 3` bytes, one RGB per frame slot starting at slot 0.
* Sparse payload: `count * 4` bytes, each entry a uint8 slot byte followed by RGB. Sparse slots are therefore limited to 0..255; the encoder rejects larger values rather than truncating them.
* A `release` message has no payload and asks the daemon to stop driving the
  keyboard so the firmware effect can resume.

Multiple senders are arbitrated last-writer-wins, matching how Aura Sync lets
several applications share one device.
