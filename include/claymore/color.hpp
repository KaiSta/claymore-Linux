/*---------------------------------------------------------*\
| color.hpp - RGB colour type for the ROG Claymore tool     |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace claymore
{
    struct Rgb
    {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;

        constexpr Rgb() = default;
        constexpr Rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
            : r(red), g(green), b(blue) {}

        bool operator==(const Rgb& other) const
        {
            return r == other.r && g == other.g && b == other.b;
        }
        bool operator!=(const Rgb& other) const { return !(*this == other); }

        bool is_black() const { return r == 0 && g == 0 && b == 0; }

        /*-----------------------------------------------------*\
        | Accepts "RRGGBB", "#RRGGBB", "0xRRGGBB" and the       |
        | human names below. Case insensitive.                  |
        \*-----------------------------------------------------*/
        static std::optional<Rgb> parse(const std::string& text);

        /*-----------------------------------------------------*\
        | h in [0, 360), s and v in [0, 1]                      |
        \*-----------------------------------------------------*/
        static Rgb from_hsv(double h, double s, double v);

        std::string to_hex() const;
    };

    /*---------------------------------------------------------*\
    | Multiplies a colour by a scale factor in [0, 1] and        |
    | rounds to nearest.                                         |
    \*---------------------------------------------------------*/
    Rgb scale(const Rgb& color, double factor);

    /*---------------------------------------------------------*\
    | Linear interpolation between two colours. t in [0, 1].     |
    \*---------------------------------------------------------*/
    Rgb lerp(const Rgb& a, const Rgb& b, double t);
}
