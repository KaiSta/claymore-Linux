/*---------------------------------------------------------*\
| protocol.cpp - ASUS Aura keyboard wire protocol           |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "claymore/protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace claymore
{
    namespace protocol
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

            /*-------------------------------------------------*\
            | Claymore effect packets always stamp 0xFF into the |
            | fifth, ninth, thirteenth and seventeenth 12 byte   |
            | block. Its meaning is undocumented but the firmware |
            | expects it, so it is applied to every effect.      |
            \*-------------------------------------------------*/
            void stamp_effect_blocks(Report& report)
            {
                for(int i = 1; i < 5; i++)
                {
                    report[5 + i * 12] = 0xFF;
                }
            }
        }

        std::uint8_t speed_from_percent(int percent)
        {
            const int clamped = std::clamp(percent, 0, 100);
            const double raw  = (100.0 - static_cast<double>(clamped)) / 100.0 *
                                static_cast<double>(SPEED_SLOWEST);
            return static_cast<std::uint8_t>(std::lround(raw));
        }

        int speed_to_percent(std::uint8_t speed)
        {
            const double percent = (static_cast<double>(SPEED_SLOWEST) - static_cast<double>(speed)) /
                                   static_cast<double>(SPEED_SLOWEST) * 100.0;
            return static_cast<int>(std::lround(percent));
        }

        Report allow_remote_control(RemoteControl type)
        {
            Report report{};
            report[0] = 0x00;
            report[1] = 0x41;
            report[2] = static_cast<std::uint8_t>(type);
            return report;
        }

        Report save_mode()
        {
            Report report{};
            report[0] = 0x00;
            report[1] = 0x50;
            report[2] = 0x55;
            return report;
        }

        Report update_mode(Mode mode)
        {
            Report report{};
            report[0] = 0x00;
            report[1] = 0x51;
            report[2] = 0x28;
            report[3] = 0x00;
            report[4] = 0x00;
            report[5] = static_cast<std::uint8_t>(mode);
            return report;
        }

        Report get_numpad_location()
        {
            Report report{};
            report[0] = 0x00;
            report[1] = 0x40;
            report[2] = 0x60;
            return report;
        }

        Report get_version_request()
        {
            Report report{};
            report[0] = 0x00;
            report[1] = 0x10;
            report[2] = 0x01;
            report[3] = 0x00;
            report[4] = 0x00;
            report[5] = 0x00; /* set to 1 to ask for the numpad firmware instead */
            return report;
        }

        Report get_version_read()
        {
            Report report{};
            report[0] = 0x00;
            report[1] = 0x12;
            report[2] = 0x22;
            return report;
        }

        std::string parse_version_reply(const std::uint8_t* reply, std::size_t length)
        {
            if(reply == nullptr || length <= 10)
            {
                return "";
            }

            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%02X.%02X.%02X", reply[8], reply[9], reply[10]);
            return std::string(buffer);
        }

        int parse_numpad_reply(const std::uint8_t* reply, std::size_t length)
        {
            if(reply == nullptr || length <= 5)
            {
                return 0;
            }
            return reply[5] * 2 + reply[4];
        }

        Report effect
            (
            Mode                        mode,
            std::uint8_t                speed,
            Direction                   direction,
            ColorMode                   color_mode,
            bool                        pattern,
            const std::vector<Rgb>&     colors,
            std::optional<std::uint8_t> lightness
            )
        {
            Report report{};
            report[0] = 0x00;
            report[1] = 0x51;
            report[2] = 0x2C;
            report[3] = 0x00;
            report[4] = 0x00;
            report[5] = static_cast<std::uint8_t>(mode);
            report[6] = speed;

            const bool random  = (color_mode == ColorMode::Random);

            /*-------------------------------------------------*\
            | A Wave with explicit colours is the firmware's      |
            | "colour wave" and must have bit 5 set. Without it   |
            | the firmware runs its own rainbow and largely       |
            | ignores the palette you sent, which looks like the  |
            | colours being wrong rather than missing.            |
            |                                                    |
            | The reference implementation reaches the same bit    |
            | by rewriting colour mode "specific" as "pattern"    |
            | for Wave only, so the rule is applied here rather   |
            | than left to every caller to remember.              |
            \*-------------------------------------------------*/
            const bool automatic_pattern = (mode == Mode::Wave) &&
                                           (color_mode == ColorMode::Specific) &&
                                           !colors.empty();

            const bool patterned = (color_mode == ColorMode::Pattern) || pattern ||
                                   automatic_pattern;

            report[7] = static_cast<std::uint8_t>((random ? 128 : 0) +
                                                  (patterned ? 32 : 0) +
                                                  static_cast<std::uint8_t>(direction));
            report[8]  = 0xFF;
            report[9]  = 0xFF;

            /*-------------------------------------------------*\
            | Light level. ColourCycle ships with 0x80 in the     |
            | reference implementation; everything else uses      |
            | 0xFF. An explicit value always wins.                |
            \*-------------------------------------------------*/
            if(lightness.has_value())
            {
                report[10] = *lightness;
            }
            else
            {
                report[10] = (mode == Mode::ColorCycle) ? 0x80 : 0xFF;
            }

            if(mode == Mode::Quicksand)
            {
                /*---------------------------------------------*\
                | Quicksand pulls its six colours from a         |
                | separate packet; the effect packet itself stays |
                | empty apart from the block stamps.              |
                \*---------------------------------------------*/
                stamp_effect_blocks(report);
                return report;
            }

            /*-------------------------------------------------*\
            | Rainbow Wave with no explicit colour is a white    |
            | gradient in firmware terms.                        |
            \*-------------------------------------------------*/
            if(mode == Mode::Wave && color_mode == ColorMode::Specific && colors.empty())
            {
                report[11] = 0xFF;
                report[12] = 0xFF;
                report[13] = 0xFF;
            }

            for(std::size_t i = 0; i < colors.size() && i < 8; i++)
            {
                report[11 + i * 3] = colors[i].r;
                report[12 + i * 3] = colors[i].g;
                report[13 + i * 3] = colors[i].b;
            }

            stamp_effect_blocks(report);
            return report;
        }

        Report quicksand_colors(const std::vector<Rgb>& colors)
        {
            Report report{};
            report[0] = 0x00;
            report[1] = 0x51;
            report[2] = 0x91;

            for(std::size_t i = 0; i < colors.size() && i < 6; i++)
            {
                report[5 + i * 3] = colors[i].r;
                report[6 + i * 3] = colors[i].g;
                report[7 + i * 3] = colors[i].b;
            }
            return report;
        }

        std::vector<Report> direct_led_packets(const std::vector<LedColor>& leds)
        {
            std::vector<Report> packets;

            const std::size_t total = leds.size();
            for(std::size_t offset = 0; offset < total; offset += DIRECT_LEDS_PER_PACKET)
            {
                Report     report{};
                const std::size_t count = std::min(DIRECT_LEDS_PER_PACKET, total - offset);

                report[0] = 0x00;
                report[1] = 0xC0;
                report[2] = 0x81;
                report[3] = static_cast<std::uint8_t>(count);
                report[4] = 0x00;

                for(std::size_t i = 0; i < count; i++)
                {
                    const LedColor& led = leds[offset + i];
                    report[i * 4 + 5] = led.index;
                    report[i * 4 + 6] = led.color.r;
                    report[i * 4 + 7] = led.color.g;
                    report[i * 4 + 8] = led.color.b;
                }

                packets.push_back(report);
            }

            return packets;
        }

        Report direct_single_led(std::uint8_t index, const Rgb& color)
        {
            Report report{};
            report[0] = 0x00;
            report[1] = 0xC0;
            report[2] = 0x81;
            report[3] = 0x01;
            report[4] = 0x00;
            report[5] = index;
            report[6] = color.r;
            report[7] = color.g;
            report[8] = color.b;
            return report;
        }

        std::string mode_name(Mode mode)
        {
            switch(mode)
            {
                case Mode::Static:      return "static";
                case Mode::Breathing:   return "breathing";
                case Mode::ColorCycle:  return "color-cycle";
                case Mode::Reactive:    return "reactive";
                case Mode::Wave:        return "wave";
                case Mode::Ripple:      return "ripple";
                case Mode::StarryNight: return "starry-night";
                case Mode::Quicksand:   return "quicksand";
                case Mode::Current:     return "current";
                case Mode::RainDrop:    return "rain-drop";
                case Mode::Direct:      return "direct";
            }
            return "unknown";
        }

        std::vector<Mode> hardware_modes()
        {
            return
            {
                Mode::Static,
                Mode::Breathing,
                Mode::ColorCycle,
                Mode::Reactive,
                Mode::Wave,
                Mode::Ripple,
                Mode::StarryNight,
                Mode::Quicksand,
                Mode::Current,
                Mode::RainDrop,
            };
        }

        bool parse_mode(const std::string& text, Mode& out)
        {
            const std::string wanted = lowered(text);
            for(Mode mode : hardware_modes())
            {
                if(wanted == mode_name(mode))
                {
                    out = mode;
                    return true;
                }
            }

            /*-------------------------------------------------*\
            | A few tolerantly accepted aliases                  |
            \*-------------------------------------------------*/
            if(wanted == "rainbow" || wanted == "spectrum") { out = Mode::ColorCycle;  return true; }
            if(wanted == "rainbow-wave" || wanted == "color-wave")
                                                            { out = Mode::Wave;        return true; }
            if(wanted == "starry")                          { out = Mode::StarryNight; return true; }
            if(wanted == "custom")                          { out = Mode::Static;      return true; }
            return false;
        }

        std::string direction_name(Direction direction)
        {
            switch(direction)
            {
                case Direction::Left:  return "left";
                case Direction::Right: return "right";
                case Direction::Up:    return "up";
                case Direction::Down:  return "down";
            }
            return "left";
        }

        bool parse_direction(const std::string& text, Direction& out)
        {
            const std::string wanted = lowered(text);
            if(wanted == "left")  { out = Direction::Left;  return true; }
            if(wanted == "right") { out = Direction::Right; return true; }
            if(wanted == "up")    { out = Direction::Up;    return true; }
            if(wanted == "down")  { out = Direction::Down;  return true; }
            return false;
        }
    }
}
