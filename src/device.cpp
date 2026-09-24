/*---------------------------------------------------------*\
| device.cpp - high level Claymore device driver            |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "claymore/device.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace claymore
{
    namespace
    {
        /*-----------------------------------------------------*\
        | The Claymore reports its key legends through the USB   |
        | bcdDevice field. These magic numbers come from the     |
        | values OpenRGB derives during SetupZones():            |
        |                                                        |
        |   bcdDevice >> 12 == 2 or 3  ->  ISO ("UK") legends    |
        |   anything else              ->  ANSI ("US") legends   |
        \*-----------------------------------------------------*/
        Region region_from_bcd(std::uint16_t bcd_device)
        {
            const unsigned nibble = (bcd_device >> 12) & 0x0Fu;
            return (nibble == 2 || nibble == 3) ? Region::UK : Region::US;
        }

        /*-----------------------------------------------------*\
        | Numpad reply encoding: byte[5] * 2 + byte[4].          |
        \*-----------------------------------------------------*/
        Numpad numpad_from_reply(int encoded)
        {
            switch(encoded)
            {
                case 2: return Numpad::Right;
                case 3: return Numpad::Left;
                default: return Numpad::None;
            }
        }

        int candidate_score(const HidrawInfo& info, const DetectOptions& options)
        {
            if(info.vendor_id != options.vendor_id || info.product_id != options.product_id)
            {
                return -1;
            }

            int score = 1;
            if(info.interface_number == options.interface_number) score += 8;
            if(info.usage_page == options.usage_page)            score += 4;
            return score;
        }
    }

    std::vector<HidrawInfo> ClaymoreDevice::find_candidates(const DetectOptions& options)
    {
        std::vector<HidrawInfo> all = enumerate_hidraw();
        std::vector<HidrawInfo> matched;

        if(!options.device_path.empty())
        {
            /*-------------------------------------------------*\
            | An explicit node bypasses VID/PID matching, which  |
            | is what you want when reverse engineering a        |
            | variant keyboard.                                  |
            \*-------------------------------------------------*/
            for(const HidrawInfo& info : all)
            {
                if(info.device_path == options.device_path ||
                   info.hidraw_name == options.device_path)
                {
                    matched.push_back(info);
                    return matched;
                }
            }

            HidrawInfo synthetic;
            synthetic.device_path = options.device_path;
            synthetic.hidraw_name = options.device_path;
            synthetic.vendor_id   = options.vendor_id;
            synthetic.product_id  = options.product_id;
            synthetic.interface_number = -1;
            matched.push_back(synthetic);
            return matched;
        }

        for(const HidrawInfo& info : all)
        {
            const int score = candidate_score(info, options);
            if(score < 0)
            {
                continue;
            }

            const bool exact = info.interface_number == options.interface_number &&
                               info.usage_page == options.usage_page;

            if(options.strict && !exact)
            {
                continue;
            }

            matched.push_back(info);
        }

        std::stable_sort(matched.begin(), matched.end(),
            [&options](const HidrawInfo& a, const HidrawInfo& b)
            {
                return candidate_score(a, options) > candidate_score(b, options);
            });

        return matched;
    }

    std::unique_ptr<ClaymoreDevice> ClaymoreDevice::open
        (
        const DetectOptions&  options,
        std::string&          error,
        std::string*          warning
        )
    {
        const std::vector<HidrawInfo> candidates = find_candidates(options);

        if(candidates.empty())
        {
            char ids[32];
            std::snprintf(ids, sizeof(ids), "%04x:%04x", options.vendor_id, options.product_id);
            error = std::string("no hidraw device matching ") + ids +
                    " (interface " + std::to_string(options.interface_number) + ")";
            return nullptr;
        }

        const HidrawInfo& chosen = candidates.front();

        std::unique_ptr<HidrawDevice> transport_device = HidrawDevice::open(chosen, error);
        if(!transport_device)
        {
            return nullptr;
        }

        if(warning != nullptr)
        {
            const bool exact = chosen.interface_number == options.interface_number &&
                               chosen.usage_page == options.usage_page;

            if(!exact)
            {
                std::ostringstream note;
                note << "using " << chosen.device_path << " (interface "
                     << chosen.interface_number << ", usage 0x" << std::hex
                     << chosen.usage_page << std::dec
                     << "), which does not match the expected interface "
                     << options.interface_number << " / usage 0x" << std::hex
                     << options.usage_page << std::dec;
                *warning = note.str();
            }
            else if(candidates.size() > 1)
            {
                *warning = std::to_string(candidates.size() - 1) +
                           " other matching hidraw node(s) ignored";
            }
        }

        std::unique_ptr<ClaymoreDevice> device(new ClaymoreDevice());
        device->transport = std::move(transport_device);

        if(!device->refresh_status(error))
        {
            return nullptr;
        }

        /*-----------------------------------------------------*\
        | Caller overrides apply after detection so that a       |
        | mis-detected unit can still be driven correctly.       |
        \*-----------------------------------------------------*/
        if(options.region.has_value())
        {
            device->device_status.region          = *options.region;
            device->device_status.region_detected = false;
        }
        if(options.numpad.has_value())
        {
            device->device_status.numpad          = *options.numpad;
            device->device_status.numpad_detected = false;
        }

        const std::string id = options.layout_id.empty()
                             ? variant_id(device->device_status.numpad, device->device_status.region)
                             : options.layout_id;

        if(!device->use_layout(id, error))
        {
            return nullptr;
        }

        return device;
    }

    ClaymoreDevice::~ClaymoreDevice()
    {
        if(release_on_destroy && direct_active && transport)
        {
            std::string ignored;
            (void)release(ignored);
        }
    }

    void ClaymoreDevice::set_ack_timeout_ms(int milliseconds)
    {
        ack_timeout_ms = std::max(0, milliseconds);
    }

    bool ClaymoreDevice::send(const protocol::Report& report, std::string& error)
    {
        transport->flush();
        return transport->write_report(report.data(), report.size(), error);
    }

    bool ClaymoreDevice::send_and_await(const protocol::Report& report, std::string& error, int timeout_ms)
    {
        if(!send(report, error))
        {
            return false;
        }

        std::uint8_t reply[protocol::REPORT_LENGTH];
        transport->read_report(reply, sizeof(reply),
                               timeout_ms >= 0 ? timeout_ms : ack_timeout_ms);
        return true;
    }

    bool ClaymoreDevice::query
        (
        const protocol::Report& request,
        std::uint8_t*           reply,
        std::size_t             reply_length,
        int                     timeout_ms,
        std::string&            error
        )
    {
        if(!send(request, error))
        {
            return false;
        }

        std::fill(reply, reply + reply_length, 0);

        /*-----------------------------------------------------*\
        | The keyboard can emit a stray report first, so keep    |
        | reading until one arrives or the timeout lapses.       |
        \*-----------------------------------------------------*/
        const int deadline = timeout_ms > 0 ? timeout_ms : ack_timeout_ms;
        const int got      = transport->read_report(reply, reply_length, deadline, &error);

        if(got < 0)
        {
            return false;
        }
        if(got == 0)
        {
            error = "keyboard did not answer in time";
            return false;
        }
        return true;
    }

    bool ClaymoreDevice::refresh_status(std::string& error)
    {
        /*-----------------------------------------------------*\
        | Nothing here is fatal: a keyboard that stays silent on  |
        | the version or numpad queries is still perfectly        |
        | usable, it just gets defaulted answers.                 |
        \*-----------------------------------------------------*/
        (void)error;

        device_status.hidraw_node = transport->info().device_path;
        device_status.bcd_device  = transport->info().bcd_device;

        /*-----------------------------------------------------*\
        | Firmware version: request, then read.                  |
        \*-----------------------------------------------------*/
        {
            std::string version_error;
            if(send_and_await(protocol::get_version_request(), version_error, 120))
            {
                std::uint8_t reply[protocol::REPORT_LENGTH];
                if(query(protocol::get_version_read(), reply, sizeof(reply), 250, version_error))
                {
                    device_status.firmware_version = protocol::parse_version_reply(reply, sizeof(reply));
                }
            }
        }

        /*-----------------------------------------------------*\
        | Numpad: absence of an answer simply means "detached".  |
        \*-----------------------------------------------------*/
        {
            std::uint8_t reply[protocol::REPORT_LENGTH];
            std::string  numpad_error;
            if(query(protocol::get_numpad_location(), reply, sizeof(reply), 250, numpad_error))
            {
                device_status.numpad          = numpad_from_reply(protocol::parse_numpad_reply(reply, sizeof(reply)));
                device_status.numpad_detected = true;
            }
            else
            {
                device_status.numpad          = Numpad::None;
                device_status.numpad_detected = false;
            }
        }

        device_status.region          = region_from_bcd(device_status.bcd_device);
        device_status.region_detected = true;

        return true;
    }

    bool ClaymoreDevice::use_layout(const std::string& variant_id_wanted, std::string& error)
    {
        const LayoutVariant* found = find_variant(variant_id_wanted);
        if(found == nullptr)
        {
            std::ostringstream message;
            message << "unknown layout '" << variant_id_wanted << "', known layouts:";
            for(const std::string& id : all_variant_ids())
            {
                message << " " << id;
            }
            error = message.str();
            return false;
        }

        active_layout                  = found;
        device_status.variant_id       = found->id;
        return true;
    }

    bool ClaymoreDevice::apply_effect(const EffectSpec& spec, std::string& error)
    {
        if(spec.mode == protocol::Mode::Direct)
        {
            return enter_direct_mode(error);
        }

        /*-----------------------------------------------------*\
        | The whole sequence comes from effect_reports() so that  |
        | the ordering and the presence of the commit packet are  |
        | covered by tests rather than by hope.                   |
        \*-----------------------------------------------------*/
        for(const protocol::Report& report : effect_reports(spec))
        {
            if(is_commit_report(report))
            {
                /*---------------------------------------------*\
                | The commit writes flash and needs real time;   |
                | the keyboard acknowledges after roughly 50 ms. |
                \*---------------------------------------------*/
                if(!send(report, error))
                {
                    return false;
                }

                std::uint8_t reply[protocol::REPORT_LENGTH];
                transport->read_report(reply, sizeof(reply), 150);
                continue;
            }

            if(!send_and_await(report, error))
            {
                return false;
            }
        }

        direct_active = false;
        return true;
    }

    bool ClaymoreDevice::enter_direct_mode(std::string& error)
    {
        if(!send_and_await(protocol::allow_remote_control(protocol::RemoteControl::Grant), error))
        {
            return false;
        }

        direct_active = true;
        if(!send_and_await(protocol::allow_remote_control(protocol::RemoteControl::GrantDirect), error))
        {
            std::string release_error;
            (void)release(release_error);
            return false;
        }


        return true;
    }

    bool ClaymoreDevice::apply_frame(const Frame& frame, std::string& error)
    {
        if(active_layout == nullptr)
        {
            error = "no layout selected";
            return false;
        }

        const std::size_t expected = static_cast<std::size_t>(active_layout->key_count);
        if(frame.size() != expected)
        {
            error = "frame has " + std::to_string(frame.size()) + " slots but layout " +
                    active_layout->id + " needs " + std::to_string(expected);
            return false;
        }

        if(!direct_active)
        {
            if(!enter_direct_mode(error))
            {
                return false;
            }
        }

        /*-----------------------------------------------------*\
        | Translate frame slots into device LED indices.         |
        \*-----------------------------------------------------*/
        std::vector<protocol::LedColor> leds;
        leds.reserve(expected);
        for(std::size_t slot = 0; slot < expected; slot++)
        {
            leds.push_back(protocol::LedColor{ active_layout->keys[slot].index, frame[slot] });
        }

        for(const protocol::Report& report : protocol::direct_led_packets(leds))
        {
            if(!send(report, error))
            {
                return false;
            }

            std::uint8_t reply[protocol::REPORT_LENGTH];
            transport->read_report(reply, sizeof(reply), ack_timeout_ms);
        }

        return true;
    }

    bool ClaymoreDevice::apply_slot(int slot, const Rgb& color, std::string& error)
    {
        if(active_layout == nullptr)
        {
            error = "no layout selected";
            return false;
        }
        if(slot < 0 || slot >= active_layout->key_count)
        {
            error = "slot " + std::to_string(slot) + " is outside the active layout (0.." +
                    std::to_string(active_layout->key_count - 1) + ")";
            return false;
        }

        if(!direct_active && !enter_direct_mode(error))
        {
            return false;
        }

        const protocol::Report report =
            protocol::direct_single_led(active_layout->keys[slot].index, color);

        if(!send(report, error))
        {
            return false;
        }

        std::uint8_t reply[protocol::REPORT_LENGTH];
        transport->read_report(reply, sizeof(reply), ack_timeout_ms);
        return true;
    }

    bool ClaymoreDevice::release(std::string& error)
    {
        if(!direct_active)
        {
            return true;
        }

        const bool sent = send_and_await(
            protocol::allow_remote_control(protocol::RemoteControl::Revoke), error);
        if(sent)
        {
            direct_active = false;
        }
        return sent;
    }

    bool ClaymoreDevice::save(std::string& error)
    {
        const protocol::Report report = protocol::save_mode();
        if(!send(report, error))
        {
            return false;
        }

        std::uint8_t reply[protocol::REPORT_LENGTH];
        transport->read_report(reply, sizeof(reply), 120);
        return true;
    }

    std::unique_ptr<ClaymoreDevice> open_first(const DetectOptions& options, std::string& error)
    {
        std::string warning;
        std::unique_ptr<ClaymoreDevice> device = ClaymoreDevice::open(options, error, &warning);

        if(device && !warning.empty())
        {
            /*-------------------------------------------------*\
            | Surface the caveat without failing the open.       |
            \*-------------------------------------------------*/
            error = warning;
        }

        return device;
    }
}
