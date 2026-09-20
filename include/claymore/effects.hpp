/*---------------------------------------------------------*\
| effects.hpp - effect settings and preview rendering       |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
|                                                           |
|   Two different things live here:                          |
|                                                           |
|   1. EffectSpec - a description of what to send to the     |
|      keyboard firmware.                                    |
|   2. EffectPreview - a software approximation of how the   |
|      firmware animates that effect. The keyboard still     |
|      does the real work; this exists so the GUI can show   |
|      a live picture of the chosen effect without needing   |
|      to read colours back off the hardware (which the      |
|      Claymore does not support).                           |
\*---------------------------------------------------------*/

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "claymore/color.hpp"
#include "claymore/layout.hpp"
#include "claymore/protocol.hpp"

namespace claymore
{
    struct EffectSpec
    {
        protocol::Mode      mode         = protocol::Mode::Static;
        std::vector<Rgb>    colors;                    /* up to 8 for most modes */
        bool                random_colors = false;
        int                 speed_percent = 50;        /* 0 = slowest, 100 = fastest */
        protocol::Direction direction     = protocol::Direction::Left;

        /*-----------------------------------------------------*\
        | Firmware light level 0..3. Empty means "do not touch", |
        | which reproduces the reference packet exactly.         |
        \*-----------------------------------------------------*/
        std::optional<int>  brightness;

        /*-----------------------------------------------------*\
        | Advisory only, and deliberately not honoured.          |
        |                                                       |
        | The 0x50/0x55 packet is not just "write this to flash" |
        | on the Claymore: it is what COMMITS the effect. Skip   |
        | it and the firmware discards the effect as soon as     |
        | remote control is released, snapping back to the       |
        | previously committed one. Measured on real hardware:   |
        | the board flashes the new colour, then reverts.       |
        |                                                       |
        | So every effect change writes flash, and there is no   |
        | way to preview an effect without committing it. See    |
        | effect_reports() below.                                |
        \*-----------------------------------------------------*/
        bool                persist = true;

        /*-----------------------------------------------------*\
        | How many colours the firmware accepts for this mode.   |
        \*-----------------------------------------------------*/
        int colors_min() const;
        int colors_max() const;

        bool uses_direction() const;
        bool uses_colors() const;
        bool supports_random() const;

        /*-----------------------------------------------------*\
        | Pad or trim `colors` to the count the firmware wants.  |
        \*-----------------------------------------------------*/
        std::vector<Rgb> normalized_colors() const;

        std::uint8_t lightness_byte() const;
    };

    /*---------------------------------------------------------*\
    | The exact reports that applying `spec` sends, in order.   |
    |                                                           |
    | This lives here rather than in the device so that the      |
    | command sequence can be asserted in tests without any      |
    | hardware, which matters: getting the ordering or a missing |
    | packet wrong here produces a keyboard that looks broken.   |
    |                                                           |
    | Sequence: grant control, upload the effect, (quicksand     |
    | colours), select the slot, commit, release control.        |
    \*---------------------------------------------------------*/
    std::vector<protocol::Report> effect_reports(const EffectSpec& spec);

    /*---------------------------------------------------------*\
    | The speed the vendor software ships for each effect, on    |
    | this tool's 0..100 scale.                                  |
    |                                                           |
    | The mapping to the firmware's 0..254 byte is linear and    |
    | 0 is the FASTEST, so the top of the range is genuinely     |
    | extreme: 90 percent is already near the limit. Defaulting   |
    | to the values ASUS chose keeps every effect looking right   |
    | without the user having to guess.                           |
    \*---------------------------------------------------------*/
    int default_speed_percent(protocol::Mode mode);

    /*---------------------------------------------------------*\
    | True when a report is the commit packet, which the         |
    | keyboard takes noticeably longer to acknowledge.           |
    \*---------------------------------------------------------*/
    bool is_commit_report(const protocol::Report& report);

    /*---------------------------------------------------------*\
    | Named presets shown in the effect picker.                  |
    \*---------------------------------------------------------*/
    struct EffectPreset
    {
        std::string   id;
        std::string   name;
        std::string   description;
        EffectSpec    spec;
    };

    std::vector<EffectPreset> builtin_presets();

    /*---------------------------------------------------------*\
    | A frame is one colour per layout frame slot, in the order  |
    | of LayoutVariant::keys.                                    |
    \*---------------------------------------------------------*/
    using Frame = std::vector<Rgb>;

    namespace preview
    {
        /*-----------------------------------------------------*\
        | Deterministic software rendering of `spec` at time `t`  |
        | seconds. Same inputs always give the same frame, so it  |
        | is safe to unit test and to drive a GUI at any rate.    |
        \*-----------------------------------------------------*/
        Frame render(const EffectSpec& spec, const LayoutVariant& layout, double t);

        /*-----------------------------------------------------*\
        | Fills a frame for a user painted design, dimmed by the  |
        | brightness setting.                                     |
        \*-----------------------------------------------------*/
        Frame render_custom(const Frame& design, const EffectSpec& spec);
    }
}
