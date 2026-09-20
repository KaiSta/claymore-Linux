/*---------------------------------------------------------*\
| protocol.hpp - ASUS Aura keyboard wire protocol           |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
|                                                           |
|   Packet layouts were reverse engineered by the OpenRGB   |
|   project and cross checked against ASUS' own Aura tooling |
|   captures. Every builder below emits one 65 byte HID      |
|   report: byte 0 is the report number (0 for the Aura      |
|   interface), bytes 1..64 are payload.                     |
|                                                           |
|   Reference: OpenRGB AsusAuraTUFKeyboardController.cpp    |
|              (C) Mola19, GPL-2.0-or-later                  |
\*---------------------------------------------------------*/

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "claymore/color.hpp"

namespace claymore
{
    namespace protocol
    {
        /*-----------------------------------------------------*\
        | Identity of the Aura control interface on the original |
        | ROG Claymore. The keyboard exposes several HID        |
        | interfaces; only this one drives the LEDs.             |
        \*-----------------------------------------------------*/
        constexpr std::uint16_t AURA_VENDOR_ID    = 0x0B05;
        constexpr std::uint16_t CLAYMORE_PRODUCT_ID = 0x184D;
        constexpr int           AURA_INTERFACE    = 1;
        constexpr std::uint16_t AURA_USAGE_PAGE   = 0xFF00;

        constexpr std::size_t REPORT_LENGTH = 65;

        using Report = std::array<std::uint8_t, REPORT_LENGTH>;

        /*-----------------------------------------------------*\
        | Lighting modes understood by the keyboard firmware.    |
        | These are hardware effects: the keyboard animates them |
        | on its own, so they cost no host CPU and survive       |
        | reboots once saved.                                    |
        \*-----------------------------------------------------*/
        enum class Mode : std::uint8_t
        {
            Static       = 0,
            Breathing    = 1,
            ColorCycle   = 2,
            Reactive     = 3,
            Wave         = 4,
            Ripple       = 5,
            StarryNight  = 6,
            Quicksand    = 7,
            Current      = 8,
            RainDrop     = 9,
            Direct       = 15,
        };

        /*-----------------------------------------------------*\
        | Direction bytes for the Wave effect. The Claymore      |
        | uses a 2 bit field: left 0, right 4, up 6, down 2.     |
        \*-----------------------------------------------------*/
        enum class Direction : std::uint8_t
        {
            Left  = 0,
            Right = 4,
            Up    = 6,
            Down  = 2,
        };

        /*-----------------------------------------------------*\
        | Colour sourcing for effects that take colours.         |
        \*-----------------------------------------------------*/
        enum class ColorMode : std::uint8_t
        {
            Specific = 0, /* use the colours the caller supplied */
            Random   = 1, /* firmware picks colours at random    */
            Pattern  = 2, /* Wave: gradient through given colours*/
        };

        /*-----------------------------------------------------*\
        | Remote control handshake values (Claymore only).       |
        | 1 = grant, 0 = revoke, 3 = grant exclusive direct mode.|
        \*-----------------------------------------------------*/
        enum class RemoteControl : std::uint8_t
        {
            Revoke         = 0,
            Grant          = 1,
            GrantDirect    = 3,
        };

        /*-----------------------------------------------------*\
        | Speed takes 0..254 where a HIGHER value is SLOWER and  |
        | 0 is the fastest. Use speed_from_percent() to convert  |
        | a friendlier 0..100 setting.                           |
        \*-----------------------------------------------------*/
        constexpr std::uint8_t SPEED_FASTEST = 0;
        constexpr std::uint8_t SPEED_SLOWEST = 254;

        std::uint8_t speed_from_percent(int percent);
        int          speed_to_percent(std::uint8_t speed);

        /*-----------------------------------------------------*\
        | Brightness is expressed as the four stepped levels the |
        | firmware exposes; level 3 is full.                     |
        \*-----------------------------------------------------*/
        constexpr int BRIGHTNESS_LEVELS = 4;

        /*-----------------------------------------------------*\
        | Packet builders. Each returns a complete report.       |
        \*-----------------------------------------------------*/
        Report allow_remote_control(RemoteControl type);

        /*-----------------------------------------------------*\
        | Persists the current effect into the keyboard's flash. |
        | The Claymore acknowledges this after roughly 50 ms.    |
        \*-----------------------------------------------------*/
        Report save_mode();

        /*-----------------------------------------------------*\
        | Claymore only: tells the firmware which effect slot is |
        | active.                                                |
        \*-----------------------------------------------------*/
        Report update_mode(Mode mode);

        /*-----------------------------------------------------*\
        | Claymore only: asks where the detachable numpad sits.  |
        | The reply encodes the answer, see numpad_from_reply(). |
        \*-----------------------------------------------------*/
        Report get_numpad_location();

        /*-----------------------------------------------------*\
        | Firmware version handshake. Write `get_version_request`|
        | first, then `get_version_read` and read the reply.     |
        \*-----------------------------------------------------*/
        Report get_version_request();
        Report get_version_read();

        std::string parse_version_reply(const std::uint8_t* reply, std::size_t length);

        /*-----------------------------------------------------*\
        | Decodes the numpad location reply into 0 (absent),     |
        | 2 (right) or 3 (left).                                 |
        \*-----------------------------------------------------*/
        int parse_numpad_reply(const std::uint8_t* reply, std::size_t length);

        /*-----------------------------------------------------*\
        | Hardware effect packet.                                |
        |                                                        |
        | `lightness` is the byte the firmware treats as a       |
        | light level rather than a brightness: leave it empty   |
        | to reproduce the reference behaviour (0xFF, or 0x80    |
        | for ColorCycle) and pass a value to override it.       |
        \*-----------------------------------------------------*/
        Report effect
            (
            Mode                    mode,
            std::uint8_t            speed,
            Direction               direction,
            ColorMode               color_mode,
            bool                    pattern,
            const std::vector<Rgb>& colors,
            std::optional<std::uint8_t> lightness = std::nullopt
            );

        /*-----------------------------------------------------*\
        | Claymore only: the Quicksand effect reads its six      |
        | colours from a separate packet that must be sent       |
        | after the effect packet.                               |
        \*-----------------------------------------------------*/
        Report quicksand_colors(const std::vector<Rgb>& colors);

        /*-----------------------------------------------------*\
        | Direct mode: one report can carry at most 15 LEDs.     |
        | Build the full series for `colors` with              |
        | direct_led_packets(). Each entry is (device LED index, |
        | colour).                                               |
        \*-----------------------------------------------------*/
        constexpr std::size_t DIRECT_LEDS_PER_PACKET = 15;

        struct LedColor
        {
            std::uint8_t index;
            Rgb          color;
        };

        std::vector<Report> direct_led_packets(const std::vector<LedColor>& leds);

        /*-----------------------------------------------------*\
        | Direct mode: address a single LED.                     |
        \*-----------------------------------------------------*/
        Report direct_single_led(std::uint8_t index, const Rgb& color);

        /*-----------------------------------------------------*\
        | Human readable mode name and parsing for the CLI / UI. |
        \*-----------------------------------------------------*/
        std::string mode_name(Mode mode);
        bool        parse_mode(const std::string& text, Mode& out);
        std::vector<Mode> hardware_modes();

        std::string   direction_name(Direction direction);
        bool          parse_direction(const std::string& text, Direction& out);
    }
}
