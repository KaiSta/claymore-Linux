/*---------------------------------------------------------*\
| color.cpp - RGB colour helpers                            |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "claymore/color.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace claymore
{
    namespace
    {
        std::string lowered(const std::string& text)
        {
            std::string out = text;
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        int hex_digit(char c)
        {
            if(c >= '0' && c <= '9') return c - '0';
            if(c >= 'a' && c <= 'f') return c - 'a' + 10;
            if(c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        const std::unordered_map<std::string, Rgb>& named_colors()
        {
            static const std::unordered_map<std::string, Rgb> table =
            {
                { "off",        Rgb(0x00, 0x00, 0x00) },
                { "black",      Rgb(0x00, 0x00, 0x00) },
                { "white",      Rgb(0xFF, 0xFF, 0xFF) },
                { "red",        Rgb(0xFF, 0x00, 0x00) },
                { "green",      Rgb(0x00, 0xFF, 0x00) },
                { "blue",       Rgb(0x00, 0x00, 0xFF) },
                { "yellow",     Rgb(0xFF, 0xFF, 0x00) },
                { "cyan",       Rgb(0x00, 0xFF, 0xFF) },
                { "magenta",    Rgb(0xFF, 0x00, 0xFF) },
                { "orange",     Rgb(0xFF, 0x66, 0x00) },
                { "purple",     Rgb(0x80, 0x00, 0xFF) },
                { "pink",       Rgb(0xFF, 0x40, 0x80) },
                { "warm-white", Rgb(0xFF, 0xD0, 0x80) },
                { "rog-red",    Rgb(0xFF, 0x00, 0x3C) },
                { "asus-cyan",  Rgb(0x00, 0xC8, 0xFF) },
            };
            return table;
        }
    }

    std::optional<Rgb> Rgb::parse(const std::string& text)
    {
        std::string value = lowered(text);

        /*-----------------------------------------------------*\
        | Strip a leading '#' or '0x'                           |
        \*-----------------------------------------------------*/
        if(!value.empty() && value[0] == '#')
        {
            value.erase(0, 1);
        }
        else if(value.rfind("0x", 0) == 0)
        {
            value.erase(0, 2);
        }

        const bool all_hex = !value.empty() &&
            std::all_of(value.begin(), value.end(), [](char c) { return hex_digit(c) >= 0; });

        if(all_hex && (value.size() == 3 || value.size() == 6))
        {
            int digits[6];
            if(value.size() == 3)
            {
                for(int i = 0; i < 3; i++)
                {
                    const int d = hex_digit(value[i]);
                    digits[i * 2]     = d;
                    digits[i * 2 + 1] = d;
                }
            }
            else
            {
                for(int i = 0; i < 6; i++)
                {
                    digits[i] = hex_digit(value[i]);
                }
            }

            return Rgb(static_cast<std::uint8_t>(digits[0] * 16 + digits[1]),
                       static_cast<std::uint8_t>(digits[2] * 16 + digits[3]),
                       static_cast<std::uint8_t>(digits[4] * 16 + digits[5]));
        }

        /*-----------------------------------------------------*\
        | "r,g,b" decimal triple                                |
        \*-----------------------------------------------------*/
        {
            int r = 0, g = 0, b = 0;
            char extra = 0;
            if(std::sscanf(value.c_str(), "%d,%d,%d%c", &r, &g, &b, &extra) == 3)
            {
                const auto clamp = [](int v)
                {
                    return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
                };
                return Rgb(clamp(r), clamp(g), clamp(b));
            }
        }

        const auto it = named_colors().find(value);
        if(it != named_colors().end())
        {
            return it->second;
        }

        return std::nullopt;
    }

    Rgb Rgb::from_hsv(double h, double s, double v)
    {
        h = std::fmod(h, 360.0);
        if(h < 0.0)
        {
            h += 360.0;
        }
        s = std::clamp(s, 0.0, 1.0);
        v = std::clamp(v, 0.0, 1.0);

        const double c = v * s;
        const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
        const double m = v - c;

        double r = 0, g = 0, b = 0;
        if(h < 60)       { r = c; g = x; }
        else if(h < 120) { r = x; g = c; }
        else if(h < 180) { g = c; b = x; }
        else if(h < 240) { g = x; b = c; }
        else if(h < 300) { r = x; b = c; }
        else             { r = c; b = x; }

        const auto to_byte = [&](double channel)
        {
            return static_cast<std::uint8_t>(std::lround((channel + m) * 255.0));
        };

        return Rgb(to_byte(r), to_byte(g), to_byte(b));
    }

    std::string Rgb::to_hex() const
    {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "%02X%02X%02X", r, g, b);
        return std::string(buffer);
    }

    Rgb scale(const Rgb& color, double factor)
    {
        const auto apply = [factor](std::uint8_t channel)
        {
            const double scaled = static_cast<double>(channel) * factor;
            return static_cast<std::uint8_t>(std::clamp(std::lround(scaled), 0L, 255L));
        };
        return Rgb(apply(color.r), apply(color.g), apply(color.b));
    }

    Rgb lerp(const Rgb& a, const Rgb& b, double t)
    {
        const auto mix = [t](std::uint8_t x, std::uint8_t y)
        {
            const double value = static_cast<double>(x) + (static_cast<double>(y) - static_cast<double>(x)) * t;
            return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
        };
        return Rgb(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b));
    }
}
