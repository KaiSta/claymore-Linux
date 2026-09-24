/*---------------------------------------------------------*\
| main.cpp - the claymore command line tool                |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <climits>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "claymore/design.hpp"
#include "claymore/device.hpp"
#include "claymore/effects.hpp"
#include "claymore/hidraw.hpp"
#include "claymore/layout.hpp"
#include "claymore/protocol.hpp"
#include "claymore/settings.hpp"
#include "claymore/sync_server.hpp"

using namespace claymore;

namespace
{
    volatile std::sig_atomic_t stop_requested = 0;

    void handle_signal(int)
    {
        stop_requested = 1;
    }

    struct Options
    {
        DetectOptions detect;

        std::vector<Rgb> colors;
        bool             random_colors = false;
        int              speed_percent = 50;
        bool             speed_set     = false;
        std::optional<int> brightness;
        protocol::Direction direction = protocol::Direction::Left;
        bool             persist       = true;
        bool             quiet         = false;
        int              ack_timeout   = 20;
        std::string      socket_path;
        int              fps           = 30;
        int              idle_timeout  = 1500;
        std::string      idle_effect   = "breathing";
        bool             demo          = false;
        bool             from_stdin    = false;
        double           seconds       = 0.0;
        std::string      frame_file;

        /*-------------------------------------------------*\
        | Single-frame painting. Every --key is composed    |
        | into ONE frame, because re-entering direct mode    |
        | clears whatever was there before.                  |
        \*-------------------------------------------------*/
        std::optional<Rgb>                                        paint_all;
        std::vector<std::pair<std::string, Rgb>>                  paint_keys;
        std::string                                               design_file;
        bool                                                      use_default_design = false;

        std::vector<std::string> positional;
    };

    void print_usage()
    {
        std::cout <<
R"(rog-claymore-linux - control the lighting of an ASUS ROG Claymore on Linux

USAGE
  claymore <command> [options]

DEVICE COMMANDS
  list                     list every hidraw node and flag the Claymore
  info                     firmware version, numpad side, key legends, layout
  keys                     every key with its frame slot and device LED index
  layout                   ASCII map of the active layout
  save                     store the current effect in the keyboard's flash  LIGHTING COMMANDS
  effect <name>            apply a firmware effect (see EFFECTS)
  preset <id>              apply a built in preset (see presets)
  presets                  list built in presets
  designs                  list saved designs, marking the startup one
  default <name>           choose the design to apply at login, or 'none'
  settings                 list the preferences the GUI shares
  settings <name> <value>  change one, e.g. start-in-tray on
  static <colour>          shortcut for 'effect static'
  off                      shortcut for 'static black'
  paint                    compose ONE frame in direct mode:
                             --all <colour>            fill the board
                             --key <name>=<colour>     repaint a key (repeatable)
                             --design <file>           load a design saved by the GUI
                             --default                 load the startup design
  set <slot|key> <colour>  repaint one key; blanks the rest of the board
  fill <colour>            paint the whole board in direct mode
  release                  hand the keyboard back to its firmware
  reset                    release, then apply a firmware effect
  preview <effect>         animate the effect in the terminal, no hardware needed

SYNC DAEMON
  syncd                    hold the keyboard in direct mode and let any
                           process stream frames over a UNIX socket

EFFECTS
  static  breathing  color-cycle  reactive  wave  ripple
  starry-night  quicksand  current  rain-drop

NOTE ON STORING EFFECTS
  The Claymore needs the 0x50/0x55 commit packet to display an effect at all:
  without it the board flashes the new effect and then reverts. So every effect
  change is stored in the keyboard. --no-save is accepted but has no effect.

NOTE ON DESIGNS
  A painted design is not an effect and the keyboard cannot store one: its
  flash holds its built in effects only. What it does do is keep the last frame
  it is given, so 'paint' leaves the design on the board without revoking
  remote control and the design stays lit with nothing running. 'release' hands
  the board back to its own effects.

  To have a design re-applied after a reboot, save it and let a systemd user
  unit run this again at login - see contrib/claymore-design.service:

    claymore paint --design ~/.config/claymore/design.txt

COMMON OPTIONS
  --color <RRGGBB|name>    add a colour, repeatable; use 'off' for black
  --random                 let the firmware pick colours at random
  --speed <0-100>          animation speed, 100 is fastest
  --brightness <0-3>       firmware light level (experimental)
  --direction <lr|rl|up|down>
  --device <path>          force a /dev/hidrawN node
  --vid <hex> --pid <hex>  match different USB identifiers
  --interface <n>          HID interface number, default 1
  --usage-page <hex>       HID usage page, default 0xff00
  --layout <id>            force a layout, see 'claymore layout --list'
  --region <us|uk>         override auto detected key legends
  --numpad <none|left|right>
  --ack-timeout <ms>       report acknowledgement timeout, default 20
  --quiet                  suppress progress messages

SYNCD OPTIONS
  --socket <path>          socket to listen on, default )" << default_socket_path() << R"(
  --fps <n>                frames per second, default 30
  --demo                   ignore the socket and animate a rainbow instead
  --stdin                  read frames from stdin instead of the socket
  --idle-effect <name>     firmware effect to restore when senders stop
  --idle-timeout <ms>      how long without frames before restoring it

EXAMPLES
  claymore info
  claymore static rog-red --persist
  claymore effect wave --color ff003c --color 00c8ff --direction left --speed 40
  claymore paint --all 300000 --key escape=00ff00 --key numpad-lock=ff00ff
  claymore paint --design claymore-design.txt
  claymore release
  claymore preview rainbow-wave
  claymore syncd --fps 30
)";
    }

    void fail(const std::string& message)
    {
        std::cerr << "claymore: " << message << "\n";
        std::exit(1);
    }

    bool parse_u16(const std::string& text, std::uint16_t& out)
    {
        if(text.empty() || text.front() == '-')
        {
            return false;
        }

        errno = 0;
        char* end = nullptr;
        const unsigned long value = std::strtoul(text.c_str(), &end, 0);
        if(errno == ERANGE || end == text.c_str() || *end != '\0' || value > 0xFFFF)
        {
            return false;
        }
        out = static_cast<std::uint16_t>(value);
        return true;
    }

    bool parse_int(const std::string& text, int& out)
    {
        if(text.empty())
        {
            return false;
        }

        errno = 0;
        char* end = nullptr;
        const long value = std::strtol(text.c_str(), &end, 0);
        if(errno == ERANGE || end == text.c_str() || *end != '\0' ||
           value < INT_MIN || value > INT_MAX)
        {
            return false;
        }
        out = static_cast<int>(value);
        return true;
    }

    /*---------------------------------------------------------*\
    | Collect argv into flags and positionals. Options that take |
    | a value are whitelisted so that e.g. 'claymore set f1 red' |
    | does not confuse a bare word for a flag value.             |
    \*---------------------------------------------------------*/
    struct ParsedArgs
    {
        std::vector<std::string> positional;
        std::vector<std::pair<std::string, std::string>> flags;
    };

    bool flag_takes_value(const std::string& name)
    {
        static const std::vector<std::string> value_flags =
        {
            "--color", "--colour", "--colors", "--speed", "--brightness", "--direction",
            "--device", "--vid", "--pid", "--interface", "--usage-page", "--layout",
            "--region", "--numpad", "--ack-timeout", "--socket", "--fps",
            "--idle-effect", "--idle-timeout", "--seconds", "--frame", "--key",
            "--all", "--design",
        };
        return std::find(value_flags.begin(), value_flags.end(), name) != value_flags.end();
    }

    ParsedArgs parse_args(int argc, char** argv, int start)
    {
        ParsedArgs parsed;
        for(int i = start; i < argc; i++)
        {
            const std::string token = argv[i];

            if(token.rfind("--", 0) == 0)
            {
                std::string name  = token;
                std::string value;

                const std::size_t equals = token.find('=');
                if(equals != std::string::npos)
                {
                    name  = token.substr(0, equals);
                    value = token.substr(equals + 1);
                }
                else if(flag_takes_value(name) && i + 1 < argc)
                {
                    value = argv[++i];
                }

                parsed.flags.emplace_back(name, value);
            }
            else if(!token.empty() && token[0] == '-' && token.size() > 1 && !std::isdigit(static_cast<unsigned char>(token[1])))
            {
                parsed.flags.emplace_back(token, "");
            }
            else
            {
                parsed.positional.push_back(token);
            }
        }
        return parsed;
    }

    bool has_flag(const ParsedArgs& args, const std::string& name)
    {
        for(const auto& [key, value] : args.flags)
        {
            if(key == name)
            {
                return true;
            }
        }
        return false;
    }

    Rgb require_color(const std::string& text)
    {
        const auto parsed = Rgb::parse(text);
        if(!parsed)
        {
            fail("'" + text + "' is not a colour; try ff003c, #ff003c or red");
        }
        return *parsed;
    }

    int resolve_slot(const LayoutVariant& variant, const std::string& query)
    {
        /*-----------------------------------------------------*\
        | A bare number is a frame slot; anything else goes      |
        | through the key table so 'f5', 'left-shift' and        |
        | '0x60' all work.                                       |
        \*-----------------------------------------------------*/
        int slot = 0;
        const bool numeric = !query.empty() &&
            std::all_of(query.begin(), query.end(), [](unsigned char c) { return std::isdigit(c) != 0; });

        if(numeric && parse_int(query, slot))
        {
            if(slot < 0 || slot >= variant.key_count)
            {
                fail("slot " + query + " is outside the layout (0.." +
                     std::to_string(variant.key_count - 1) + ")");
            }
            return slot;
        }

        const KeyName* key = find_key(variant, query);
        if(key == nullptr)
        {
            fail("no key called '" + query + "' in layout " + variant.id +
                 "; run 'claymore keys' to see them");
        }

        for(int i = 0; i < variant.key_count; i++)
        {
            if(&variant.keys[i] == key)
            {
                return i;
            }
        }
        fail("internal error resolving key '" + query + "'");
        return 0;
    }

    void apply_common_flags(Options& options, const ParsedArgs& args)
    {
        for(const auto& [name, value] : args.flags)
        {
            if(name == "--color" || name == "--colour" || name == "--colors")
            {
                std::stringstream stream(value);
                std::string       piece;
                while(std::getline(stream, piece, ','))
                {
                    if(piece.empty())
                    {
                        continue;
                    }
                    const auto color = Rgb::parse(piece);
                    if(!color)
                    {
                        fail("'" + piece + "' is not a colour");
                    }
                    options.colors.push_back(*color);
                }
            }
            else if(name == "--random")
            {
                options.random_colors = true;
            }
            else if(name == "--speed")
            {
                if(!parse_int(value, options.speed_percent) ||
                   options.speed_percent < 0 || options.speed_percent > 100)
                {
                    fail("--speed must be between 0 and 100");
                }
                options.speed_set = true;
            }
            else if(name == "--brightness")
            {
                int level = 0;
                if(!parse_int(value, level) || level < 0 || level >= protocol::BRIGHTNESS_LEVELS)
                {
                    fail("--brightness must be 0, 1, 2 or 3");
                }
                options.brightness = level;
            }
            else if(name == "--direction")
            {
                protocol::Direction direction;
                if(!protocol::parse_direction(value, direction))
                {
                    fail("--direction must be left, right, up or down");
                }
                options.direction = direction;
            }
            else if(name == "--persist" || name == "--save")
            {
                options.persist = true;
            }
            else if(name == "--no-save" || name == "--no-persist")
            {
                /*---------------------------------------------*\
                | Kept so old scripts do not break, but the      |
                | keyboard cannot show an uncommitted effect.    |
                \*---------------------------------------------*/
                options.persist = true;
                std::cerr << "claymore: --no-save is ignored: the Claymore only shows an effect "
                          << "once it is committed to the keyboard\n";
            }
            else if(name == "--quiet")
            {
                options.quiet = true;
            }
            else if(name == "--device")
            {
                options.detect.device_path = value;
            }
            else if(name == "--vid")
            {
                if(!parse_u16(value, options.detect.vendor_id)) fail("--vid needs a hex number");
            }
            else if(name == "--pid")
            {
                if(!parse_u16(value, options.detect.product_id)) fail("--pid needs a hex number");
            }
            else if(name == "--interface")
            {
                if(!parse_int(value, options.detect.interface_number)) fail("--interface needs a number");
            }
            else if(name == "--usage-page")
            {
                if(!parse_u16(value, options.detect.usage_page)) fail("--usage-page needs a hex number");
            }
            else if(name == "--layout")
            {
                options.detect.layout_id = value;
            }
            else if(name == "--region")
            {
                if(value == "us" || value == "ansi")      options.detect.region = Region::US;
                else if(value == "uk" || value == "iso")  options.detect.region = Region::UK;
                else fail("--region must be us or uk");
            }
            else if(name == "--numpad")
            {
                if(value == "none")       options.detect.numpad = Numpad::None;
                else if(value == "left")  options.detect.numpad = Numpad::Left;
                else if(value == "right") options.detect.numpad = Numpad::Right;
                else fail("--numpad must be none, left or right");
            }
            else if(name == "--ack-timeout")
            {
                if(!parse_int(value, options.ack_timeout) || options.ack_timeout < 0)
                    fail("--ack-timeout must be zero or greater");
            }
            else if(name == "--strict")
            {
                options.detect.strict = true;
            }
            else if(name == "--socket")
            {
                options.socket_path = value;
            }
            else if(name == "--fps")
            {
                if(!parse_int(value, options.fps) || options.fps <= 0) fail("--fps needs a positive number");
            }
            else if(name == "--idle-effect")
            {
                options.idle_effect = value;
            }
            else if(name == "--idle-timeout")
            {
                if(!parse_int(value, options.idle_timeout) || options.idle_timeout < 0)
                    fail("--idle-timeout must be zero or greater");
            }
            else if(name == "--seconds")
            {
                char* end = nullptr;
                options.seconds = std::strtod(value.c_str(), &end);
                if(end == value.c_str() || *end != '\0' ||
                   !std::isfinite(options.seconds) || options.seconds < 0.0)
                    fail("--seconds must be a finite non-negative number");
            }
            else if(name == "--frame")
            {
                options.frame_file = value;
            }
            else if(name == "--demo")
            {
                options.demo = true;
            }
            else if(name == "--stdin")
            {
                options.from_stdin = true;
            }
            else if(name == "--all")
            {
                options.paint_all = require_color(value);
            }
            else if(name == "--key")
            {
                const std::size_t equals = value.find('=');
                if(equals == std::string::npos)
                {
                    fail("--key wants key=colour, e.g. --key escape=ff0000");
                }
                options.paint_keys.emplace_back(value.substr(0, equals),
                                               require_color(value.substr(equals + 1)));
            }
            else if(name == "--design")
            {
                options.design_file = value;
            }
            else if(name == "--default")
            {
                options.use_default_design = true;
            }
            else if(name == "--help" || name == "-h")
            {
                print_usage();
                std::exit(0);
            }
        }
    }

    EffectSpec spec_from_options(const Options& options, protocol::Mode mode)
    {
        EffectSpec spec;
        spec.mode          = mode;
        spec.colors        = options.colors;
        spec.random_colors = options.random_colors;
        spec.speed_percent = options.speed_set ? options.speed_percent
                                              : default_speed_percent(mode);
        spec.direction     = options.direction;
        spec.brightness    = options.brightness;
        spec.persist       = options.persist;
        return spec;
    }

    std::unique_ptr<ClaymoreDevice> connect_device(const Options& options, bool required = true)
    {
        std::string error;
        std::unique_ptr<ClaymoreDevice> device = ClaymoreDevice::open(options.detect, error);

        if(!device)
        {
            if(required)
            {
                fail(error + "\n  hint: run 'claymore list' to see what is present");
            }
            return nullptr;
        }

        device->set_ack_timeout_ms(options.ack_timeout);

        if(!options.quiet)
        {
            std::cerr << "claymore: using " << device->status().hidraw_node
                      << ", firmware " << (device->status().firmware_version.empty()
                                             ? std::string("unknown")
                                             : device->status().firmware_version)
                      << ", layout " << device->layout().id << "\n";
        }
        return device;
    }

    /*===================================================================*\
    | Terminal preview                                                    |
    \*===================================================================*/

    std::string ansi_for(const Rgb& color)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "\033[48;2;%d;%d;%dm",
                      color.r, color.g, color.b);
        return std::string(buffer);
    }

    void draw_frame(const LayoutVariant& variant, const Frame& frame)
    {
        std::ostringstream out;
        out << "\033[H"; /* cursor home */

        for(int row = 0; row < variant.rows; row++)
        {
            /*-------------------------------------------------*\
            | Blank row for vertical breathing space between      |
            | key rows, so the ASCII keyboard looks like a        |
            | keyboard.                                           |
            \*-------------------------------------------------*/
            for(int line = 0; line < 2; line++)
            {
                for(int col = 0; col < variant.cols; col++)
                {
                    const int slot = variant.matrix[row * variant.cols + col];
                    if(slot < 0)
                    {
                        out << "    ";
                    }
                    else
                    {
                        const Rgb& color = frame[static_cast<std::size_t>(slot)];
                        out << ansi_for(color) << "    " << "\033[0m";
                    }
                }
                out << "\033[K\n";
            }
        }

        std::cout << out.str() << std::flush;
    }

    int command_preview(const Options& options, const std::string& effect_name)
    {
        protocol::Mode mode = protocol::Mode::Static;
        if(!protocol::parse_mode(effect_name, mode))
        {
            fail("unknown effect '" + effect_name + "'; try one of: " +
                 [] {
                     std::string list;
                     for(protocol::Mode m : protocol::hardware_modes())
                     {
                         if(!list.empty()) list += " ";
                         list += protocol::mode_name(m);
                     }
                     return list;
                 }());
        }

        const EffectSpec spec = spec_from_options(options, mode);

        /*-----------------------------------------------------*\
        | Preview needs a layout but not a keyboard, so pick the |
        | detected one when available and a sane default if not. |
        \*-----------------------------------------------------*/
        std::string layout_id = options.detect.layout_id;
        if(layout_id.empty())
        {
            const DetectOptions& detect = options.detect;
            std::string error;
            std::unique_ptr<ClaymoreDevice> device = ClaymoreDevice::open(detect, error);

            if(device)
            {
                layout_id = device->layout().id;
            }
            else
            {
                const Numpad numpad = options.detect.numpad.value_or(Numpad::None);
                const Region region = options.detect.region.value_or(Region::US);
                layout_id = variant_id(numpad, region);
                std::cerr << "claymore: no keyboard found, previewing layout "
                          << layout_id << " (" << error << ")\n";
            }
        }

        const LayoutVariant* variant = find_variant(layout_id);
        if(variant == nullptr)
        {
            fail("unknown layout '" + layout_id + "'");
        }

        std::cout << "\033[2J\033[?25l"; /* clear screen, hide cursor */

        const auto start = std::chrono::steady_clock::now();
        std::signal(SIGINT, handle_signal);

        const int led_count = variant->key_count;
        std::cout << "\n " << effect_name << " on " << variant->id
                  << "  (ctrl-c to stop)\n\n";

        while(stop_requested == 0)
        {
            const auto now = std::chrono::steady_clock::now();
            const double t = std::chrono::duration<double>(now - start).count();

            draw_frame(*variant, preview::render(spec, *variant, t));

            if(options.seconds > 0.0 && t >= options.seconds)
            {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1000 / 30));
        }

        (void)led_count;
        std::cout << "\033[?25h\033[0m\n";
        return 0;
    }

    /*===================================================================*\
    | Commands                                                            |
    \*===================================================================*/

    int command_list()
    {
        const std::vector<HidrawInfo> devices = enumerate_hidraw();

        if(devices.empty())
        {
            std::cout << "no hidraw devices found. Is /sys mounted and are you in the plugdev group?\n";
            return 0;
        }

        bool found = false;
        for(const HidrawInfo& info : devices)
        {
            const bool is_claymore = info.vendor_id == protocol::AURA_VENDOR_ID &&
                                     info.product_id == protocol::CLAYMORE_PRODUCT_ID;

            std::cout << (is_claymore ? "  * " : "    ") << describe(info);
            if(is_claymore)
            {
                found = true;
                if(info.interface_number == protocol::AURA_INTERFACE)
                {
                    std::cout << "  <- Aura control interface";
                }
            }
            std::cout << "\n";
        }

        if(!found)
        {
            std::cout << "\nNo ASUS ROG Claymore (0b05:184d) present. ";
            if(const char* hint = std::getenv("USER"))
            {
                std::cout << "Check the udev rule is installed, " << hint << ".";
            }
            std::cout << "\n";
        }

        return 0;
    }

    int command_info(const Options& options)
    {
        std::unique_ptr<ClaymoreDevice> device = connect_device(options);
        const DeviceStatus& status = device->status();

        std::cout << "device          " << status.hidraw_node << "\n";
        std::cout << "usb id          " << std::hex
                  << device->transport_info().vendor_id << ":"
                  << device->transport_info().product_id << std::dec << "\n";
        std::cout << "firmware        "
                  << (status.firmware_version.empty() ? "unknown" : status.firmware_version) << "\n";
        std::cout << "bcdDevice       0x" << std::hex << status.bcd_device << std::dec
                  << " (bcd nibble " << ((status.bcd_device >> 12) & 0xF) << ")\n";
        std::cout << "key legends     " << (status.region == Region::UK ? "ISO / uk" : "ANSI / us")
                  << (status.region_detected ? " (auto)" : " (overridden)") << "\n";
        std::cout << "numpad          ";
        switch(status.numpad)
        {
            case Numpad::None:  std::cout << "not attached";  break;
            case Numpad::Left:  std::cout << "attached left"; break;
            case Numpad::Right: std::cout << "attached right"; break;
        }
        std::cout << (status.numpad_detected ? " (reported by keyboard)" : " (assumed)") << "\n";
        std::cout << "layout          " << status.variant_id << "\n";
        std::cout << "leds            " << device->layout().key_count << "\n";
        std::cout << "matrix          " << device->layout().rows << " rows x "
                  << device->layout().cols << " cols\n";
        std::cout << "direct mode     " << (device->direct_mode_active() ? "active" : "idle") << "\n";
        return 0;
    }

    int command_keys(const Options& options)
    {
        std::unique_ptr<ClaymoreDevice> device = connect_device(options);
        const LayoutVariant& variant = device->layout();

        std::cout << "layout " << variant.id << "\n\n";
        std::printf("  %-5s %-20s %-22s %s\n", "slot", "device led", "name", "cli name");
        for(int slot = 0; slot < variant.key_count; slot++)
        {
            const KeyName& key = variant.keys[slot];
            char index[16];
            std::snprintf(index, sizeof(index), "0x%02X (%d)", key.index, key.index);
            std::printf("  %-5d %-20s %-22s %s\n", slot, index, key.name, key.slug);
        }
        return 0;
    }

    int command_layout(const Options& options, const ParsedArgs& args)
    {
        if(has_flag(args, "--list"))
        {
            for(const std::string& id : all_variant_ids())
            {
                std::cout << id << "\n";
            }
            return 0;
        }

        std::unique_ptr<ClaymoreDevice> device = connect_device(options);
        std::cout << render_layout(device->layout());
        return 0;
    }

    int command_effect(const Options& options, const std::string& effect_name)
    {
        protocol::Mode mode = protocol::Mode::Static;
        if(!protocol::parse_mode(effect_name, mode))
        {
            fail("unknown effect '" + effect_name + "'");
        }

        const EffectSpec spec = spec_from_options(options, mode);

        if(spec.uses_colors() && options.colors.empty() && !options.random_colors &&
           spec.colors_min() > 0)
        {
            fail("effect '" + effect_name + "' needs at least one --color");
        }

        std::unique_ptr<ClaymoreDevice> device = connect_device(options);

        std::string error;
        if(!device->apply_effect(spec, error))
        {
            fail(error);
        }

        if(!options.quiet)
        {
            std::cerr << "claymore: " << effect_name << " applied and stored in the keyboard\n";
        }
        return 0;
    }

    int command_presets()
    {
        for(const EffectPreset& preset : builtin_presets())
        {
            std::cout << "  " << preset.id << "\n      " << preset.name
                      << " - " << preset.description << "\n";
        }
        return 0;
    }

    int command_preset(const Options& options, const std::string& id)
    {
        for(const EffectPreset& preset : builtin_presets())
        {
            if(preset.id != id)
            {
                continue;
            }

            EffectSpec spec = preset.spec;
            spec.persist = options.persist;

            if(options.brightness.has_value()) spec.brightness = options.brightness;
            if(options.speed_set)              spec.speed_percent = options.speed_percent;

            /*---------------------------------------------*\
            | An explicit --color overrides the preset.      |
            \*---------------------------------------------*/
            if(!options.colors.empty()) spec.colors = options.colors;
            if(options.random_colors)   spec.random_colors = true;

            std::unique_ptr<ClaymoreDevice> device = connect_device(options);
            std::string error;
            if(!device->apply_effect(spec, error))
            {
                fail(error);
            }

            if(!options.quiet)
            {
                std::cerr << "claymore: preset " << id << " (" << preset.name
                          << ") applied at speed " << spec.speed_percent << "%\n";
            }
            return 0;
        }

        fail("unknown preset '" + id + "'; run 'claymore presets' to list them");
        return 1;
    }

    int command_set(const Options& options, const std::string& target, const Rgb& color)
    {
        std::unique_ptr<ClaymoreDevice> device = connect_device(options);
        device->set_release_on_destroy(true);
        const LayoutVariant& variant = device->layout();
        const int slot = resolve_slot(variant, target);

        std::string error;
        if(!device->apply_slot(slot, color, error))
        {
            fail(error);
        }

        if(!options.quiet)
        {
            std::cerr << "claymore: " << variant.keys[slot].name << " (" << variant.keys[slot].slug
                      << ") set to " << color.to_hex() << "\n"
                      << "claymore: note that this repainted the whole board, so every other key "
                      << "is now off; use 'claymore paint' instead\n";
        }
        return 0;
    }

    int command_fill(const Options& options, const Rgb& color)
    {
        std::unique_ptr<ClaymoreDevice> device = connect_device(options);
        device->set_release_on_destroy(true);
        const Frame frame(static_cast<std::size_t>(device->layout().key_count), color);

        std::string error;
        if(!device->apply_frame(frame, error))
        {
            fail(error);
        }

        if(!options.quiet)
        {
            std::cerr << "claymore: filled all " << frame.size()
                      << " LEDs; use 'claymore paint' to fill and set individual keys "
                      << "in the same frame\n";
        }
        return 0;
    }

    /*---------------------------------------------------------*\
    | Paint the board in direct mode. Everything is composed     |
    | into a single frame and sent in one session, because       |
    | re-entering direct mode blanks the keyboard: two separate   |
    | 'claymore set' calls would leave only the second key lit.  |
    |                                                            |
    | The session is deliberately not revoked. The Claymore keeps |
    | the last frame it was given, so leaving it alone is what    |
    | makes a design survive this process exiting - which is what |
    | lets a systemd unit re-apply a design at login and have it  |
    | simply stay.                                                |
    \*---------------------------------------------------------*/
    int command_paint(const Options& options)
    {
        if(!options.paint_all.has_value() && options.paint_keys.empty() &&
           options.design_file.empty() && !options.use_default_design)
        {
            fail("nothing to paint; use --all <colour>, --key <name>=<colour>, "
                 "--design <file> or --default");
        }

        std::unique_ptr<ClaymoreDevice> device = connect_device(options);
        const LayoutVariant& variant = device->layout();

        Frame frame(static_cast<std::size_t>(variant.key_count), Rgb(0, 0, 0));

        if(options.paint_all.has_value())
        {
            std::fill(frame.begin(), frame.end(), *options.paint_all);
        }

        if(options.use_default_design)
        {
            /*-------------------------------------------------*\
            | The design the user chose to have at login. This is |
            | what a systemd user unit runs, so it has to be a    |
            | single command with no arguments to remember.       |
            \*-------------------------------------------------*/
            std::string error;
            std::string name;
            if(!load_default_design(frame, name, error))
            {
                fail(error + " (set one with 'claymore default <name>')");
            }

            if(!options.quiet)
            {
                std::cerr << "claymore: applying the startup design '" << name << "'\n";
            }
        }

        if(!options.design_file.empty())
        {
            std::string error;
            if(!load_design_file(options.design_file, frame, error))
            {
                fail(error);
            }
        }

        for(const auto& [name, color] : options.paint_keys)
        {
            frame[static_cast<std::size_t>(resolve_slot(variant, name))] = color;
        }

        std::string error;
        if(!device->apply_frame(frame, error))
        {
            fail(error);
        }

        if(!options.quiet)
        {
            std::cerr << "claymore: painted " << variant.key_count
                      << " LEDs in one frame\n"
                      << "claymore: the design stays on the keyboard after this exits; "
                      << "run 'claymore release' to hand it back to the firmware\n";

            if(!options.design_file.empty())
            {
                std::cerr << "claymore: to have it applied at login, point a systemd user unit "
                          << "at " << options.design_file << "\n"
                          << "          see contrib/claymore-design.service\n";
            }
        }
        return 0;
    }

    /*---------------------------------------------------------*\
    | The designs the user has saved, and which one is loaded at  |
    | login.                                                      |
    \*---------------------------------------------------------*/
    int command_designs()
    {
        const std::vector<std::string> names = list_designs();
        const std::string              startup = default_design_name();

        std::cout << "  saved designs in " << designs_dir() << "\n";

        if(names.empty())
        {
            std::cout << "    (none yet - paint one in the GUI and press Save)\n";
            return 0;
        }

        for(const std::string& name : names)
        {
            std::cout << (name == startup ? "  * " : "    ") << name << "\n";
        }

        if(startup.empty())
        {
            std::cout << "\n  no design is applied at login; pick one with "
                      << "'claymore default <name>'\n";
        }

        return 0;
    }

    /*---------------------------------------------------------*\
    | The preferences the GUI and the CLI share. Reading them is  |
    | useful on its own ("why does it keep starting in the        |
    | panel?"), and writing them from a script is how an install  |
    | or an autostart entry sets its own defaults.                |
    \*---------------------------------------------------------*/
    int command_settings(const ParsedArgs& args)
    {
        if(args.positional.empty())
        {
            const std::vector<std::pair<std::string, std::string>> current = list_settings();

            std::cout << "  settings in " << settings_path() << "\n";

            if(current.empty())
            {
                std::cout << "    (none set; every setting is on its default)\n";
            }

            for(const auto& entry : current)
            {
                std::cout << "    " << entry.first << " = " << entry.second << "\n";
            }

            std::cout << "\n  " << SETTING_START_IN_TRAY << " = "
                      << (start_in_tray() ? "on" : "off")
                      << "   (start the GUI in the panel, no window)\n";
            return 0;
        }

        const std::string key = args.positional[0];

        /*-----------------------------------------------------*\
        | One key is a query, two is a change. Reading a single   |
        | setting is what a shell script wants, and this way it   |
        | does not need a second command name.                    |
        \*-----------------------------------------------------*/
        if(args.positional.size() == 1)
        {
            const std::string value = setting_get(key);
            std::cout << key << " = " << (value.empty() ? std::string("(unset)") : value) << "\n";
            return 0;
        }

        const std::string raw = args.positional[1];
        const bool        off = (raw == "off" || raw == "no" || raw == "false" || raw == "0");

        std::string error;

        /*-----------------------------------------------------*\
        | 'start-in-tray' gets its values checked because it is   |
        | the one setting with a meaning the user can get wrong;  |
        | everything else is stored as typed.                     |
        \*-----------------------------------------------------*/
        if(key == SETTING_START_IN_TRAY)
        {
            if(!set_start_in_tray(!off, error))
            {
                fail(error);
            }

            std::cout << SETTING_START_IN_TRAY << " = " << (off ? "off" : "on") << "\n";

            if(!off)
            {
                std::cout << "  claymore-gui will now start in the panel with no window;\n"
                          << "  click the icon, or run 'claymore-gui --window', to see it\n";
            }

            return 0;
        }

        if(!setting_set(key, raw, error))
        {
            fail(error);
        }

        std::cout << key << " = " << raw << "\n";
        return 0;
    }

    int command_default(const ParsedArgs& args)
    {
        /*-----------------------------------------------------*\
        | Positional arguments start after the command word, so   |
        | 'claymore default Desk' arrives as a single entry.      |
        \*-----------------------------------------------------*/
        if(args.positional.empty())
        {
            const std::string startup = default_design_name();
            if(startup.empty())
            {
                std::cout << "no design is applied at login\n";
            }
            else
            {
                std::cout << "design applied at login: " << startup << "\n";
            }
            return 0;
        }

        const std::string name = args.positional[0];
        const bool        none = (name == "none" || name == "off" || name == "-");

        std::string error;
        if(!set_default_design(none ? std::string() : name, error))
        {
            fail(error);
        }

        if(none)
        {
            std::cout << "no design will be applied at login\n";
            return 0;
        }

        std::cout << "'" << design_name_from(name) << "' will be applied at login by:\n"
                  << "  claymore paint --default\n";

        if(!std::filesystem::exists(design_path_for(design_name_from(name))))
        {
            std::cerr << "claymore: note: no design file exists yet at "
                      << design_path_for(design_name_from(name)) << "\n";
        }

        return 0;
    }

    /*---------------------------------------------------------*\
    | Give the keyboard back to its own firmware. Use this if a  |
    | direct mode session left the board dark.                   |
    \*---------------------------------------------------------*/
    int command_release(const Options& options)
    {
        std::unique_ptr<ClaymoreDevice> device = connect_device(options);

        std::string error;
        if(!device->release(error))
        {
            fail(error);
        }

        if(!options.quiet)
        {
            std::cerr << "claymore: remote control released, the firmware is in charge again\n";
        }
        return 0;
    }

    /*---------------------------------------------------------*\
    | Release remote control and then apply a firmware effect, so |
    | the keyboard always ends up in a known, lit state.          |
    \*---------------------------------------------------------*/
    int command_reset(const Options& options)
    {
        std::unique_ptr<ClaymoreDevice> device = connect_device(options);

        std::string ignored;
        device->release(ignored);

        EffectSpec spec;
        spec.mode          = protocol::Mode::Static;
        spec.colors        = options.colors.empty()
                           ? std::vector<Rgb>{ Rgb(0xFF, 0x00, 0x3C) }
                           : options.colors;
        spec.speed_percent = options.speed_percent;
        spec.brightness    = options.brightness;
        spec.persist       = options.persist;

        std::string error;
        if(!device->apply_effect(spec, error))
        {
            fail(error);
        }

        if(!options.quiet)
        {
            std::cerr << "claymore: reset " << spec.colors.front().to_hex()
                      << " and stored in the keyboard\n";
        }
        return 0;
    }

    int command_save(const Options& options)
    {
        std::unique_ptr<ClaymoreDevice> device = connect_device(options);
        std::string error;
        if(!device->save(error))
        {
            fail(error);
        }
        if(!options.quiet)
        {
            std::cerr << "claymore: current effect re-committed to the keyboard\n";
        }
        return 0;
    }

    /*===================================================================*\
    | syncd                                                               |
    \*===================================================================*/

    /*---------------------------------------------------------*\
    | Reads the documented wire format from stdin, one message   |
    | per line of hex for convenience. --stdin also accepts raw  |
    | datagrams, which are detected by their magic.              |
    \*---------------------------------------------------------*/
    bool read_frame_from_stdin(Frame& frame, bool& release)
    {
        std::string line;
        if(!std::getline(std::cin, line))
        {
            return false;
        }

        while(!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        {
            line.pop_back();
        }
        if(line.empty())
        {
            return false;
        }

        std::vector<std::uint8_t> bytes;
        bytes.reserve(line.size() / 2);

        if(line.rfind("RGBC", 0) != 0)
        {
            /*---------------------------------------------*\
            | Plain hex, may contain spaces or commas        |
            \*---------------------------------------------*/
            std::string cleaned;
            for(char c : line)
            {
                if(std::isxdigit(static_cast<unsigned char>(c)))
                {
                    cleaned.push_back(c);
                }
            }
            if(cleaned.size() % 2 != 0)
            {
                return false;
            }
            for(std::size_t i = 0; i < cleaned.size(); i += 2)
            {
                bytes.push_back(static_cast<std::uint8_t>(
                    std::strtol(cleaned.substr(i, 2).c_str(), nullptr, 16)));
            }
        }
        else
        {
            for(std::size_t i = 0; i < line.size(); i++)
            {
                bytes.push_back(static_cast<std::uint8_t>(line[i]));
            }
        }

        SyncMessage message;
        std::string error;
        if(!sync_decode(bytes.data(), bytes.size(), message, error))
        {
            std::cerr << "claymore: ignoring line: " << error << "\n";
            return false;
        }

        release = message.release;
        if(release)
        {
            return true;
        }

        for(const SyncUpdate& update : message.updates)
        {
            if(update.slot >= 0 && static_cast<std::size_t>(update.slot) < frame.size())
            {
                frame[static_cast<std::size_t>(update.slot)] = update.color;
            }
        }
        return true;
    }

    Frame demo_frame(const LayoutVariant& variant, double t)
    {
        EffectSpec spec;
        spec.mode          = protocol::Mode::Wave;
        spec.speed_percent = 60;
        spec.direction     = protocol::Direction::Left;
        return preview::render(spec, variant, t);
    }

    int command_syncd(const Options& options)
    {
        std::unique_ptr<ClaymoreDevice> device = connect_device(options);
        device->set_release_on_destroy(true);
        const LayoutVariant& variant = device->layout();

        const std::string socket_path = options.socket_path.empty()
                                      ? default_socket_path()
                                      : options.socket_path;

        std::unique_ptr<SyncServer> server;
        if(!options.demo)
        {
            std::string error;
            server = SyncServer::create(socket_path, variant.key_count, error);
            if(!server)
            {
                fail(error);
            }

            std::cout << "claymore: streaming frames on " << server->path()
                      << " (" << variant.key_count << " slots, " << options.fps << " fps)\n"
                      << "claymore: send datagrams with the 8 byte RGBC header, "
                      << "or run 'claymore syncd --demo'\n";
        }
        else
        {
            std::cout << "claymore: demo mode, animating internally\n";
        }

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        Frame       frame(static_cast<std::size_t>(variant.key_count), Rgb(0, 0, 0));
        const auto  start        = std::chrono::steady_clock::now();
        auto        last_message = std::chrono::steady_clock::now();
        bool        driving      = options.demo;
        bool        idle_applied = false;

        const auto frame_budget = std::chrono::microseconds(1000000 / options.fps);

        while(stop_requested == 0)
        {
            const auto tick_start = std::chrono::steady_clock::now();
            const double t = std::chrono::duration<double>(tick_start - start).count();

            if(options.demo)
            {
                frame = demo_frame(variant, t);
                driving = true;
            }
            else if(options.from_stdin)
            {
                if(read_frame_from_stdin(frame, driving))
                {
                    last_message = tick_start;
                }
            }
            else
            {
                std::string error;
                const int applied = server->poll(std::min(50, 1000000 / options.fps / 1000), error);
                if(applied < 0)
                {
                    std::cerr << "claymore: " << error << "\n";
                    break;
                }
                if(applied > 0)
                {
                    last_message = tick_start;
                }

                frame   = server->frame();
                driving = server->is_driving();
            }

            /*---------------------------------------------*\
            | Hand the keyboard back to its own firmware     |
            | once nobody is sending anything.              |
            \*---------------------------------------------*/
            const auto idle_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(tick_start - last_message).count();

            const bool should_stream = driving && idle_ms < options.idle_timeout;
            const bool should_idle   = !should_stream && !options.idle_effect.empty() &&
                                       options.idle_effect != "none";

            std::string error;
            if(should_stream)
            {
                if(!device->apply_frame(frame, error))
                {
                    std::cerr << "claymore: " << error << "\n";
                    break;
                }
                idle_applied = false;
            }
            else if(should_idle && !idle_applied)
            {
                protocol::Mode mode = protocol::Mode::Breathing;
                if(!protocol::parse_mode(options.idle_effect, mode))
                {
                    mode = protocol::Mode::Breathing;
                }

                EffectSpec spec;
                spec.mode          = mode;
                spec.colors        = { Rgb(0xFF, 0x00, 0x3C) };
                spec.speed_percent = 40;
                spec.persist       = false;

                if(device->apply_effect(spec, error))
                {
                    idle_applied = true;
                }
                else
                {
                    std::cerr << "claymore: " << error << "\n";
                    break;
                }
            }

            const auto elapsed = std::chrono::steady_clock::now() - tick_start;
            if(elapsed < frame_budget)
            {
                std::this_thread::sleep_for(frame_budget - elapsed);
            }
        }

        /*-----------------------------------------------------*\
        | Never leave the keyboard dark. While we were streaming |
        | direct frames the firmware was paused, so hand control |
        | back on the way out.                                   |
        \*-----------------------------------------------------*/
        if(driving)
        {
            std::string error;
            if(device->release(error))
            {
                std::cout << "claymore: released remote control, the firmware is in charge again\n";
            }
        }

        std::cout << "claymore: stopped\n";
        return 0;
    }
}

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        print_usage();
        return 1;
    }

    const std::string command = argv[1];

    if(command == "help" || command == "--help" || command == "-h")
    {
        print_usage();
        return 0;
    }

    const ParsedArgs args = parse_args(argc, argv, 2);

    Options options;
    apply_common_flags(options, args);
    options.positional = args.positional;

    try
    {
        if(command == "list")     return command_list();
        if(command == "info")     return command_info(options);
        if(command == "keys")     return command_keys(options);
        if(command == "layout")   return command_layout(options, args);
        if(command == "presets")  return command_presets();
        if(command == "designs")  return command_designs();
        if(command == "default")  return command_default(args);
        if(command == "settings") return command_settings(args);
        if(command == "save")     return command_save(options);

        if(command == "off")
        {
            options.colors = { Rgb(0, 0, 0) };
            options.positional.insert(options.positional.begin(), "static");
            return command_effect(options, "static");
        }

        if(command == "static")
        {
            if(!options.positional.empty())
            {
                options.colors.insert(options.colors.begin(), require_color(options.positional[0]));
            }
            return command_effect(options, "static");
        }

        if(command == "effect")
        {
            if(options.positional.empty())
            {
                fail("'claymore effect' needs an effect name");
            }
            return command_effect(options, options.positional[0]);
        }

        if(command == "preset")
        {
            if(options.positional.empty())
            {
                fail("'claymore preset' needs a preset id");
            }
            return command_preset(options, options.positional[0]);
        }

        if(command == "preview")
        {
            if(options.positional.empty())
            {
                fail("'claymore preview' needs an effect name");
            }
            return command_preview(options, options.positional[0]);
        }

        if(command == "set")
        {
            if(options.positional.size() < 2)
            {
                fail("usage: claymore set <slot|key> <colour>");
            }
            return command_set(options, options.positional[0], require_color(options.positional[1]));
        }

        if(command == "paint")   return command_paint(options);
        if(command == "release") return command_release(options);
        if(command == "reset")   return command_reset(options);

        if(command == "fill")
        {
            if(options.positional.empty())
            {
                fail("usage: claymore fill <colour>");
            }
            return command_fill(options, require_color(options.positional[0]));
        }

        if(command == "syncd" || command == "daemon")
        {
            return command_syncd(options);
        }

        fail("unknown command '" + command + "'; run 'claymore help'");
    }
    catch(const std::exception& ex)
    {
        fail(std::string("unexpected error: ") + ex.what());
    }

    return 0;
}
