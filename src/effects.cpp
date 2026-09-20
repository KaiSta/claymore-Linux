/*---------------------------------------------------------*\
| effects.cpp - effect settings and preview rendering       |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "claymore/effects.hpp"

#include <algorithm>
#include <cmath>

namespace claymore
{
    namespace
    {
        /*-----------------------------------------------------*\
        | Small deterministic hash so previews are reproducible. |
        \*-----------------------------------------------------*/
        std::uint32_t hash32(std::uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7FEB352Du;
            x ^= x >> 15;
            x *= 0x846CA68Bu;
            x ^= x >> 16;
            return x;
        }

        double rand01(std::uint32_t seed)
        {
            return static_cast<double>(hash32(seed)) / 4294967296.0;
        }

        double frac(double value)
        {
            return value - std::floor(value);
        }

        /*-----------------------------------------------------*\
        | Effect cadence in cycles per second for a 0..100 speed |
        | setting. Quadratic so the top of the range feels fast. |
        \*-----------------------------------------------------*/
        double rate_for(const EffectSpec& spec, double slowest, double fastest)
        {
            const double t = std::clamp(spec.speed_percent, 0, 100) / 100.0;
            return slowest + t * t * (fastest - slowest);
        }

        double brightness_factor(const EffectSpec& spec)
        {
            if(!spec.brightness.has_value())
            {
                return 1.0;
            }
            const int level = std::clamp(*spec.brightness, 0, protocol::BRIGHTNESS_LEVELS - 1);
            return (level + 1.0) / static_cast<double>(protocol::BRIGHTNESS_LEVELS);
        }

        /*-----------------------------------------------------*\
        | Samples a repeating palette at position `pos` in       |
        | "colour units", wrapping around.                       |
        \*-----------------------------------------------------*/
        Rgb sample_palette(const std::vector<Rgb>& palette, double pos)
        {
            if(palette.empty())
            {
                return Rgb(0, 0, 0);
            }
            if(palette.size() == 1)
            {
                return palette[0];
            }

            const double wrapped = frac(pos / static_cast<double>(palette.size())) *
                                   static_cast<double>(palette.size());
            const std::size_t index = static_cast<std::size_t>(wrapped) % palette.size();
            const std::size_t next  = (index + 1) % palette.size();
            return lerp(palette[index], palette[next], wrapped - std::floor(wrapped));
        }

        struct Geometry
        {
            double row = 0, col = 0;
            double row_norm = 0, col_norm = 0;
            double diagonal_norm = 0;
            double center_distance = 0;
        };

        Geometry geometry_for(const LayoutVariant& layout, int slot)
        {
            Geometry geo;
            const auto position = find_key_pos(layout, slot);
            if(!position)
            {
                return geo;
            }

            geo.row = position->row;
            geo.col = position->col;
            geo.row_norm = static_cast<double>(position->row) /
                           std::max(1, layout.rows - 1);
            geo.col_norm = static_cast<double>(position->col) /
                           std::max(1, layout.cols - 1);
            geo.diagonal_norm = (geo.row_norm + geo.col_norm) / 2.0;

            const double center_row = static_cast<double>(layout.rows - 1) / 2.0;
            const double center_col = static_cast<double>(layout.cols - 1) / 2.0;
            const double dr = (geo.row - center_row) / std::max(1.0, center_row);
            const double dc = (geo.col - center_col) / std::max(1.0, center_col);
            geo.center_distance = std::sqrt(dr * dr + dc * dc) / std::sqrt(2.0);
            return geo;
        }

        /*-----------------------------------------------------*\
        | How far along the motion axis a key sits, in 0..1.     |
        \*-----------------------------------------------------*/
        double axis_position(const Geometry& geo, protocol::Direction direction)
        {
            switch(direction)
            {
                case protocol::Direction::Left:
                case protocol::Direction::Right: return geo.col_norm;
                case protocol::Direction::Up:
                case protocol::Direction::Down:  return geo.row_norm;
            }
            return geo.col_norm;
        }

        bool axis_is_reversed(protocol::Direction direction)
        {
            return direction == protocol::Direction::Right ||
                   direction == protocol::Direction::Down;
        }
    }

    int EffectSpec::colors_min() const
    {
        switch(mode)
        {
            case protocol::Mode::ColorCycle:  return 0;
            case protocol::Mode::Quicksand:   return 6;
            case protocol::Mode::Wave:        return 0; /* 0 == rainbow */
            default:                          return 1;
        }
    }

    int EffectSpec::colors_max() const
    {
        switch(mode)
        {
            case protocol::Mode::Static:      return 1;
            case protocol::Mode::Breathing:   return 2;
            case protocol::Mode::ColorCycle:  return 0;
            case protocol::Mode::Reactive:    return 2;
            case protocol::Mode::Wave:        return 7;
            case protocol::Mode::Ripple:      return 8;
            case protocol::Mode::StarryNight: return 3;
            case protocol::Mode::Quicksand:   return 6;
            case protocol::Mode::Current:     return 3;
            case protocol::Mode::RainDrop:    return 3;
            case protocol::Mode::Direct:      return 1;
        }
        return 1;
    }

    bool EffectSpec::uses_direction() const
    {
        return mode == protocol::Mode::Wave || mode == protocol::Mode::Quicksand;
    }

    bool EffectSpec::uses_colors() const
    {
        return colors_max() > 0;
    }

    bool EffectSpec::supports_random() const
    {
        switch(mode)
        {
            case protocol::Mode::Reactive:
            case protocol::Mode::Ripple:
            case protocol::Mode::StarryNight:
            case protocol::Mode::Quicksand:
            case protocol::Mode::Current:
            case protocol::Mode::RainDrop:    return true;
            default:                          return false;
        }
    }

    std::vector<Rgb> EffectSpec::normalized_colors() const
    {
        std::vector<Rgb> out = colors;

        const int min_colors = colors_min();
        const int max_colors = colors_max();

        if(max_colors == 0)
        {
            return {};
        }

        if(static_cast<int>(out.size()) > max_colors)
        {
            out.resize(static_cast<std::size_t>(max_colors));
        }

        /*-----------------------------------------------------*\
        | Pad up to the minimum, cycling through what we have so |
        | the firmware always sees a full palette.               |
        \*-----------------------------------------------------*/
        if(!out.empty() && static_cast<int>(out.size()) < min_colors)
        {
            std::vector<Rgb> source = out;
            while(static_cast<int>(out.size()) < min_colors)
            {
                out.push_back(source[out.size() % source.size()]);
            }
        }

        return out;
    }

    std::uint8_t EffectSpec::lightness_byte() const
    {
        if(!brightness.has_value())
        {
            return (mode == protocol::Mode::ColorCycle) ? 0x80 : 0xFF;
        }

        const int level = std::clamp(*brightness, 0, protocol::BRIGHTNESS_LEVELS - 1);
        return static_cast<std::uint8_t>(level * (255 / (protocol::BRIGHTNESS_LEVELS - 1)));
    }

    std::vector<protocol::Report> effect_reports(const EffectSpec& spec)
    {
        std::vector<protocol::Report> reports;

        /*-----------------------------------------------------*\
        | Take control of the lighting for the duration of the   |
        | update.                                                |
        \*-----------------------------------------------------*/
        reports.push_back(protocol::allow_remote_control(protocol::RemoteControl::Grant));

        const std::vector<Rgb> colors = spec.normalized_colors();

        reports.push_back(protocol::effect(
            spec.mode,
            protocol::speed_from_percent(spec.speed_percent),
            spec.direction,
            spec.random_colors ? protocol::ColorMode::Random : protocol::ColorMode::Specific,
            /* pattern */ false,
            colors,
            spec.brightness.has_value() ? std::optional<std::uint8_t>(spec.lightness_byte())
                                        : std::nullopt));

        if(spec.mode == protocol::Mode::Quicksand)
        {
            reports.push_back(protocol::quicksand_colors(colors));
        }

        /*-----------------------------------------------------*\
        | Select the effect slot.                                |
        \*-----------------------------------------------------*/
        reports.push_back(protocol::update_mode(spec.mode));

        /*-----------------------------------------------------*\
        | Commit it. This is NOT optional: without it the        |
        | keyboard reverts to the previously committed effect     |
        | when the next packet releases remote control, so the    |
        | effect flashes and then disappears.                     |
        \*-----------------------------------------------------*/
        reports.push_back(protocol::save_mode());

        /*-----------------------------------------------------*\
        | Hand control back so the firmware animates the effect. |
        \*-----------------------------------------------------*/
        reports.push_back(protocol::allow_remote_control(protocol::RemoteControl::Revoke));

        return reports;
    }

    bool is_commit_report(const protocol::Report& report)
    {
        return report[1] == 0x50 && report[2] == 0x55;
    }

    int default_speed_percent(protocol::Mode mode)
    {
        /*-----------------------------------------------------*\
        | Raw defaults as shipped by the vendor software:        |
        | breathing 107, spectrum cycle 121, reactive 56, wave   |
        | 50, ripple 108, starry night 54, quicksand 103.        |
        | Passed through speed_to_percent() so the two scales    |
        | cannot drift apart.                                    |
        |                                                       |
        | Current and Rain Drop have no published default, so    |
        | they get a mid paced 90.                               |
        \*-----------------------------------------------------*/
        switch(mode)
        {
            case protocol::Mode::Breathing:   return protocol::speed_to_percent(107);
            case protocol::Mode::ColorCycle:  return protocol::speed_to_percent(121);
            case protocol::Mode::Reactive:    return protocol::speed_to_percent(56);
            case protocol::Mode::Wave:        return protocol::speed_to_percent(50);
            case protocol::Mode::Ripple:      return protocol::speed_to_percent(108);
            case protocol::Mode::StarryNight: return protocol::speed_to_percent(54);
            case protocol::Mode::Quicksand:   return protocol::speed_to_percent(103);
            case protocol::Mode::Current:     return protocol::speed_to_percent(90);
            case protocol::Mode::RainDrop:    return protocol::speed_to_percent(90);
            case protocol::Mode::Static:
            case protocol::Mode::Direct:      return 0;
        }
        return protocol::speed_to_percent(127);
    }

    std::vector<EffectPreset> builtin_presets()
    {
        std::vector<EffectPreset> presets;

        const auto add = [&presets](const char* id, const char* name, const char* description,
                                    EffectSpec spec)
        {
            presets.push_back(EffectPreset{ id, name, description, std::move(spec) });
        };

        {
            EffectSpec spec;
            spec.mode   = protocol::Mode::Static;
            spec.colors = { Rgb(0xFF, 0x00, 0x3C) };
            add("rog-red", "ROG Red", "Signature Republic of Gamers red, held steady.", spec);
        }
        {
            EffectSpec spec;
            spec.mode   = protocol::Mode::Static;
            spec.colors = { Rgb(0x00, 0xC8, 0xFF) };
            add("aurora", "Aurora", "Cool cyan wash for a quiet desk.", spec);
        }
        {
            EffectSpec spec;
            spec.mode          = protocol::Mode::Breathing;
            spec.colors        = { Rgb(0xFF, 0x00, 0x3C), Rgb(0x14, 0x00, 0x24) };
            spec.speed_percent = 45;
            add("breathing-rog", "Breathing ROG",
                "Pulses between ROG red and a deep plum.", spec);
        }
        {
            EffectSpec spec;
            spec.mode          = protocol::Mode::ColorCycle;
            spec.speed_percent = 55;
            add("spectrum", "Spectrum Cycle",
                "Sweeps the whole hue wheel across the board.", spec);
        }
        {
            EffectSpec spec;
            spec.mode          = protocol::Mode::Wave;
            spec.colors        = { Rgb(0xFF, 0x00, 0x3C), Rgb(0xFF, 0x88, 0x00), Rgb(0x00, 0xC8, 0xFF) };
            spec.direction     = protocol::Direction::Left;
            spec.speed_percent = 40;
            add("sunset-wave", "Sunset Wave",
                "A slow gradient rolling left across the keys.", spec);
        }
        {
            EffectSpec spec;
            spec.mode          = protocol::Mode::Wave;
            spec.speed_percent = 60;
            spec.direction     = protocol::Direction::Left;
            add("rainbow-wave", "Rainbow Wave",
                "Vendor rainbow wave sweeping across the keys.", spec);
        }
        {
            EffectSpec spec;
            spec.mode          = protocol::Mode::Reactive;
            spec.colors        = { Rgb(0xFF, 0xFF, 0xFF), Rgb(0x00, 0x00, 0x00) };
            spec.speed_percent = 65;
            add("reactive", "Reactive",
                "Keys flare white under your fingers, then fade out.", spec);
        }
        {
            EffectSpec spec;
            spec.mode          = protocol::Mode::Ripple;
            spec.colors        = { Rgb(0x00, 0xC8, 0xFF), Rgb(0x00, 0x00, 0x00) };
            spec.speed_percent = 50;
            add("ripple", "Ripple",
                "Rings expand outwards every time you press a key.", spec);
        }
        {
            EffectSpec spec;
            spec.mode          = protocol::Mode::StarryNight;
            spec.colors        = { Rgb(0xFF, 0xFF, 0xFF), Rgb(0x08, 0x0C, 0x20) };
            spec.speed_percent = 35;
            add("starry-night", "Starry Night",
                "A sparse field of keys twinkling over a midnight backdrop.", spec);
        }
        {
            EffectSpec spec;
            spec.mode          = protocol::Mode::Quicksand;
            spec.direction     = protocol::Direction::Down;
            spec.colors        =
            {
                Rgb(0x1B, 0x00, 0x3B), Rgb(0x3A, 0x00, 0x6E), Rgb(0xFF, 0x00, 0x3C),
                Rgb(0xFF, 0x7A, 0x00), Rgb(0x00, 0xC8, 0xFF), Rgb(0x00, 0x33, 0x22),
            };
            spec.speed_percent = 40;
            add("quicksand", "Quicksand",
                "Layers of colour sinking down through the board.", spec);
        }
        {
            EffectSpec spec;
            spec.mode          = protocol::Mode::Current;
            spec.colors        = { Rgb(0x00, 0xC8, 0xFF), Rgb(0x00, 0x22, 0x44) };
            spec.speed_percent = 45;
            add("current", "Current",
                "Bands of light drifting across the keyboard.", spec);
        }
        {
            EffectSpec spec;
            spec.mode          = protocol::Mode::RainDrop;
            spec.colors        = { Rgb(0xE0, 0xF4, 0xFF), Rgb(0x00, 0x00, 0x00) };
            spec.speed_percent = 55;
            add("rain-drop", "Rain Drop",
                "Droplets streak downwards through the keys.", spec);
        }

        return presets;
    }

    namespace preview
    {
        Frame render(const EffectSpec& spec, const LayoutVariant& layout, double t)
        {
            Frame frame(static_cast<std::size_t>(layout.key_count), Rgb(0, 0, 0));

            const std::vector<Rgb> palette_raw = spec.normalized_colors();
            std::vector<Rgb>       palette     = palette_raw.empty()
                                               ? std::vector<Rgb>{ Rgb(0xFF, 0xFF, 0xFF) }
                                               : palette_raw;

            const double dim = brightness_factor(spec);
            const int    slot_count = layout.key_count;

            switch(spec.mode)
            {
                case protocol::Mode::Static:
                case protocol::Mode::Direct:
                {
                    Rgb color = palette_raw.empty() ? Rgb(0, 0, 0) : palette_raw[0];
                    if(spec.random_colors)
                    {
                        color = Rgb::from_hsv(rand01(0x5EEDu) * 360.0, 1.0, 1.0);
                    }
                    std::fill(frame.begin(), frame.end(), scale(color, dim));
                    break;
                }

                case protocol::Mode::Breathing:
                {
                    const double hz = rate_for(spec, 0.15, 1.4);
                    const double wave = 0.5 + 0.5 * std::sin(2.0 * M_PI * hz * t);

                    Rgb color;
                    if(palette_raw.size() >= 2)
                    {
                        color = lerp(palette_raw[1], palette_raw[0], wave);
                    }
                    else
                    {
                        color = scale(palette_raw[0], wave);
                    }
                    std::fill(frame.begin(), frame.end(), scale(color, dim));
                    break;
                }

                case protocol::Mode::ColorCycle:
                {
                    const double hz = rate_for(spec, 0.03, 0.35);
                    Rgb color = Rgb::from_hsv(frac(hz * t) * 360.0, 1.0, 1.0);
                    std::fill(frame.begin(), frame.end(), scale(color, dim));
                    break;
                }

                case protocol::Mode::Wave:
                {
                    const double hz = rate_for(spec, 0.05, 0.9);

                    for(int slot = 0; slot < slot_count; slot++)
                    {
                        const Geometry geo = geometry_for(layout, slot);
                        double position = axis_position(geo, spec.direction);
                        if(axis_is_reversed(spec.direction))
                        {
                            position = 1.0 - position;
                        }

                        Rgb color;
                        if(palette_raw.empty())
                        {
                            /*---------------------------------*\
                            | Firmware rainbow wave: hue follows  |
                            | position and time                   |
                            \*---------------------------------*/
                            color = Rgb::from_hsv(frac(position + hz * t) * 360.0, 1.0, 1.0);
                        }
                        else
                        {
                            color = sample_palette(palette_raw, position * palette_raw.size() * 1.5 -
                                                                  hz * t * palette_raw.size());
                        }
                        frame[static_cast<std::size_t>(slot)] = scale(color, dim);
                    }
                    break;
                }

                case protocol::Mode::Reactive:
                {
                    const double hz   = rate_for(spec, 0.4, 4.0);
                    const double fade = 0.45;

                    for(int slot = 0; slot < slot_count; slot++)
                    {
                        const std::uint32_t seed = static_cast<std::uint32_t>(slot) * 2654435761u;
                        const double offset = rand01(seed);
                        const double cycle  = frac((t + offset) * hz / 12.0);
                        const double energy = std::max(0.0, 1.0 - cycle / fade);

                        if(spec.random_colors)
                        {
                            const Rgb flare = Rgb::from_hsv(rand01(seed ^ 0xA5A5u) * 360.0, 1.0, 1.0);
                            frame[static_cast<std::size_t>(slot)] = scale(flare, energy * dim);
                        }
                        else
                        {
                            const Rgb base = palette_raw.empty() ? Rgb(0, 0, 0)
                                                                 : palette_raw.back();
                            const Rgb flare = palette_raw.empty() ? Rgb(0xFF, 0xFF, 0xFF)
                                                                  : palette_raw[0];
                            frame[static_cast<std::size_t>(slot)] =
                                scale(lerp(base, flare, energy), dim);
                        }
                    }
                    break;
                }

                case protocol::Mode::Ripple:
                {
                    const double hz = rate_for(spec, 0.3, 2.5);

                    for(int slot = 0; slot < slot_count; slot++)
                    {
                        const Geometry geo = geometry_for(layout, slot);
                        const double cycle = frac(t * hz);
                        const double width = 0.25;
                        const double delta = std::fabs(geo.center_distance - cycle);
                        const double energy = delta < width
                                            ? std::cos(delta / width * M_PI) * 0.5 + 0.5
                                            : 0.0;

                        const Rgb base  = palette_raw.size() > 1 ? palette_raw[1] : Rgb(0, 0, 0);
                        const Rgb flare = palette_raw.empty() ? Rgb(0xFF, 0xFF, 0xFF) : palette_raw[0];
                        frame[static_cast<std::size_t>(slot)] =
                            scale(lerp(base, flare, energy), dim);
                    }
                    break;
                }

                case protocol::Mode::StarryNight:
                {
                    const double hz = rate_for(spec, 0.3, 3.0);

                    for(int slot = 0; slot < slot_count; slot++)
                    {
                        const std::uint32_t seed = static_cast<std::uint32_t>(slot) * 0x9E3779B1u;

                        /*-----------------------------*\
                        | Only a fraction of keys twinkle |
                        \*-----------------------------*/
                        const bool star = rand01(seed) < 0.28;
                        double energy = 0.0;
                        if(star)
                        {
                            const double phase = rand01(seed ^ 0x1234u);
                            energy = std::pow(std::max(0.0, std::sin(2.0 * M_PI * hz * (t + phase))), 4.0);
                        }

                        const Rgb base  = palette_raw.size() > 1 ? palette_raw[1] : Rgb(0x08, 0x0C, 0x20);
                        const Rgb flare = palette_raw.empty() ? Rgb(0xFF, 0xFF, 0xFF) : palette_raw[0];
                        frame[static_cast<std::size_t>(slot)] =
                            scale(lerp(base, flare, energy), dim);
                    }
                    break;
                }

                case protocol::Mode::Quicksand:
                {
                    const double hz = rate_for(spec, 0.05, 0.7);

                    for(int slot = 0; slot < slot_count; slot++)
                    {
                        const Geometry geo = geometry_for(layout, slot);
                        double position = geo.row_norm;
                        if(spec.direction == protocol::Direction::Up)
                        {
                            position = 1.0 - position;
                        }

                        const Rgb color = sample_palette(palette_raw.empty()
                                                       ? std::vector<Rgb>{ Rgb(0xFF, 0xFF, 0xFF) }
                                                       : palette_raw,
                                                       position * 3.0 + t * hz * 3.0);
                        frame[static_cast<std::size_t>(slot)] = scale(color, dim);
                    }
                    break;
                }

                case protocol::Mode::Current:
                {
                    const double hz = rate_for(spec, 0.08, 1.0);

                    for(int slot = 0; slot < slot_count; slot++)
                    {
                        const Geometry geo = geometry_for(layout, slot);
                        const double position = geo.diagonal_norm;
                        const Rgb color = sample_palette(palette.empty()
                                                       ? std::vector<Rgb>{ Rgb(0x00, 0xC8, 0xFF) }
                                                       : palette_raw,
                                                       position * 2.0 - t * hz * 2.0);
                        frame[static_cast<std::size_t>(slot)] = scale(color, dim);
                    }
                    break;
                }

                case protocol::Mode::RainDrop:
                {
                    const double hz = rate_for(spec, 0.3, 3.0);

                    for(int slot = 0; slot < slot_count; slot++)
                    {
                        const Geometry geo = geometry_for(layout, slot);
                        const std::uint32_t seed = static_cast<std::uint32_t>(slot) * 2246822519u;
                        const double offset = rand01(seed);
                        const double fall   = frac(t * hz * 0.25 + offset);
                        const double delta  = std::fabs(geo.row_norm - (1.0 - fall));
                        const double energy = delta < 0.12 ? 1.0 - delta / 0.12 : 0.0;

                        const Rgb base  = palette_raw.size() > 1 ? palette_raw[1] : Rgb(0, 0, 0);
                        const Rgb flare = palette_raw.empty() ? Rgb(0xE0, 0xF4, 0xFF) : palette_raw[0];
                        frame[static_cast<std::size_t>(slot)] =
                            scale(lerp(base, flare, energy), dim);
                    }
                    break;
                }
            }

            return frame;
        }

        Frame render_custom(const Frame& design, const EffectSpec& spec)
        {
            Frame frame = design;
            const double dim = brightness_factor(spec);
            for(Rgb& color : frame)
            {
                color = scale(color, dim);
            }
            return frame;
        }
    }
}
