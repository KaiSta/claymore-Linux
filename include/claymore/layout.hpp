/*---------------------------------------------------------*\
| layout.hpp - per-key LED layout tables for the Claymore   |
|                                                           |
|   Part of Claymore@Linux                                  |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
|                                                           |
|   The layout tables in layout_data.cpp are derived from   |
|   the OpenRGB project (Controllers/AsusAuraUSBController/ |
|   AsusAuraTUFKeyboardController), (C) Mola19, GPL-2.0+.   |
\*---------------------------------------------------------*/

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace claymore
{
    /*---------------------------------------------------------*\
    | Where the detachable numpad is mounted. Reported by the    |
    | keyboard itself (protocol command 0x40/0x60).              |
    \*---------------------------------------------------------*/
    enum class Numpad
    {
        None,
        Right,
        Left,
    };

    /*---------------------------------------------------------*\
    | ANSI ("US") versus ISO ("UK") key legends. The keyboard    |
    | reports this through its USB bcdDevice.                    |
    \*---------------------------------------------------------*/
    enum class Region
    {
        US,
        UK,
    };

    /*---------------------------------------------------------*\
    | One physical key / LED.                                    |
    |                                                            |
    | `index` is the device LED value, i.e. the byte that goes   |
    | into the per-LED direct-mode packet. It is NOT the same as |
    | the key's position in the frame array - see below.         |
    \*---------------------------------------------------------*/
    struct KeyName
    {
        const char* name;   /* human readable, e.g. "Left Shift"   */
        const char* slug;   /* CLI friendly, e.g. "left-shift"     */
        std::uint8_t index; /* device LED value                    */
    };

    /*---------------------------------------------------------*\
    | A key layout variant.                                      |
    |                                                            |
    | Protocol notes that shape this model:                      |
    |                                                            |
    |  * matrix is row-major, `rows * cols` entries, each one    |
    |    holding an index into `keys` (i.e. a frame slot), or    |
    |    -1 for "no key here".                                   |
    |  * keys[] is in frame order. A "frame" is simply one Rgb   |
    |    per frame slot, so a dense frame is 3 * led_count bytes |
    |    rather than 3 * 255.                                    |
    \*---------------------------------------------------------*/
    struct LayoutVariant
    {
        const char*        id;        /* "numpad-right-us"        */
        int                rows;
        int                cols;
        const std::int8_t* matrix;
        const KeyName*     keys;
        int                key_count; /* == led_count             */
    };

    /*---------------------------------------------------------*\
    | Canonical variant id, e.g. "numpad-left-uk".               |
    \*---------------------------------------------------------*/
    std::string variant_id(Numpad numpad, Region region);

    /*---------------------------------------------------------*\
    | Look up a variant by id. Returns nullptr when unknown.     |
    \*---------------------------------------------------------*/
    const LayoutVariant* find_variant(const std::string& id);

    std::vector<std::string> all_variant_ids();

    /*---------------------------------------------------------*\
    | Frame slot -> matrix position and back.                    |
    \*---------------------------------------------------------*/
    struct KeyPos
    {
        int row;
        int col;
    };

    std::optional<KeyPos> find_key_pos(const LayoutVariant& variant, int frame_slot);

    /*---------------------------------------------------------*\
    | Geometry of the on screen key grid.                        |
    |                                                            |
    | The canvas paints from these numbers and hit tests against  |
    | the same ones, so a click can never disagree with what the  |
    | user can see. It lives here, away from GTK, so the arithmetic |
    | can be unit tested: "clicking a key does nothing" is a bug  |
    | you otherwise only find with a mouse.                       |
    \*---------------------------------------------------------*/
    struct KeyGrid
    {
        double padding = 10.0; /* edge of the canvas to the first key */
        double gap     = 3.0;  /* space between neighbouring keys      */
        double radius  = 4.0;  /* corner rounding when painted         */
    };

    struct KeyRect
    {
        double x      = 0.0;
        double y      = 0.0;
        double width  = 0.0;
        double height = 0.0;
    };

    /*---------------------------------------------------------*\
    | Position of one key inside a canvas of the given size.     |
    \*---------------------------------------------------------*/
    KeyRect key_rect(const LayoutVariant& variant, int row, int col,
                     double canvas_width, double canvas_height, const KeyGrid& grid);

    /*---------------------------------------------------------*\
    | Which key is under (px, py)? Returns a frame slot, or -1.  |
    |                                                            |
    | A point is accepted when it lands on a key, or within      |
    | `snap` pixels of one. Those gaps are only a few pixels      |
    | wide, so without the tolerance a click that lands between  |
    | two keys looks exactly like a canvas that ignores clicks.  |
    \*---------------------------------------------------------*/
    int hit_test_slot(const LayoutVariant& variant, double px, double py,
                      double canvas_width, double canvas_height,
                      const KeyGrid& grid, double snap);

    /*---------------------------------------------------------*\
    | Resolve a key by slug ("left-shift"), case-insensitive     |
    | display name, or raw device index ("0x45" / "69").         |
    \*---------------------------------------------------------*/
    const KeyName* find_key(const LayoutVariant& variant, const std::string& query);

    /*---------------------------------------------------------*\
    | Render the layout as ASCII art. `frame` may hold one Rgb   |
    | per frame slot to colour the art; pass nullptr for a       |
    | plain map of device indices.                               |
    \*---------------------------------------------------------*/
    std::string render_layout(const LayoutVariant& variant);
}
