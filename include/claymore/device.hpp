/*---------------------------------------------------------*\
| device.hpp - high level Claymore device driver            |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "claymore/effects.hpp"
#include "claymore/hidraw.hpp"
#include "claymore/layout.hpp"
#include "claymore/protocol.hpp"

namespace claymore
{
    struct DetectOptions
    {
        std::uint16_t vendor_id      = protocol::AURA_VENDOR_ID;
        std::uint16_t product_id     = protocol::CLAYMORE_PRODUCT_ID;
        int           interface_number = protocol::AURA_INTERFACE;
        std::uint16_t usage_page     = protocol::AURA_USAGE_PAGE;

        /*-----------------------------------------------------*\
        | Force a specific /dev/hidrawN, skipping VID/PID match. |
        \*-----------------------------------------------------*/
        std::string   device_path;

        /*-----------------------------------------------------*\
        | Require interface and usage page to match exactly.     |
        | Off by default so that units exposing the Aura         |
        | collection on another interface still work.            |
        \*-----------------------------------------------------*/
        bool          strict = false;

        /*-----------------------------------------------------*\
        | Override auto detection of the key legends / numpad.   |
        \*-----------------------------------------------------*/
        std::optional<Region> region;
        std::optional<Numpad> numpad;
        std::string           layout_id;
    };

    struct DeviceStatus
    {
        std::string firmware_version;
        std::string hidraw_node;
        std::uint16_t bcd_device     = 0;
        Numpad      numpad           = Numpad::None;
        Region      region           = Region::US;
        std::string variant_id;
        bool        region_detected  = false; /* derived from bcdDevice rather than overridden */
        bool        numpad_detected  = false; /* answered by the keyboard */
    };

    class ClaymoreDevice
    {
    public:
        /*-----------------------------------------------------*\
        | Every hidraw node that could be the Claymore's Aura    |
        | interface, best match first.                           |
        \*-----------------------------------------------------*/
        static std::vector<HidrawInfo> find_candidates(const DetectOptions& options);

        static std::unique_ptr<ClaymoreDevice> open
            (
            const DetectOptions&  options,
            std::string&          error,
            std::string*          warning = nullptr
            );

        ~ClaymoreDevice();

        ClaymoreDevice(const ClaymoreDevice&)            = delete;
        ClaymoreDevice& operator=(const ClaymoreDevice&) = delete;

        const DeviceStatus&   status() const { return device_status; }
        const LayoutVariant&  layout() const { return *active_layout; }
        const HidrawInfo&     transport_info() const { return transport->info(); }

        /*-----------------------------------------------------*\
        | Re-runs the firmware / numpad / region handshake.      |
        \*-----------------------------------------------------*/
        bool refresh_status(std::string& error);

        /*-----------------------------------------------------*\
        | Overrides decided after the initial detection.         |
        \*-----------------------------------------------------*/
        bool use_layout(const std::string& variant_id, std::string& error);

        /*-----------------------------------------------------*\
        | Firmware effect. Grants remote control, uploads the    |
        | effect, selects the slot, optionally saves, then hands |
        | control back.                                          |
        \*-----------------------------------------------------*/
        bool apply_effect(const EffectSpec& spec, std::string& error);

        /*-----------------------------------------------------*\
        | Direct mode: streams per-key colours. The handshake is |
        | done once and remembered, so calling apply_frame() in a |
        | loop costs one write per 15 LEDs.                      |
        \*-----------------------------------------------------*/
        bool enter_direct_mode(std::string& error);
        bool apply_frame(const Frame& frame, std::string& error);

        /*-----------------------------------------------------*\
        | Direct mode: repaint a single slot.                    |
        \*-----------------------------------------------------*/
        bool apply_slot(int slot, const Rgb& color, std::string& error);

        /*-----------------------------------------------------*\
        | Persist the current effect in the keyboard's flash.    |
        \*-----------------------------------------------------*/
        bool save(std::string& error);

        /*-----------------------------------------------------*\
        | Hand control back to the firmware.                    |
        |                                                       |
        | Always call this before exiting if the keyboard was    |
        | left in direct mode. A granted remote control session  |
        | that is never released leaves the board dark, which is |
        | an easy way to make a keyboard look broken.            |
        \*-----------------------------------------------------*/
        bool release(std::string& error);

        /*-----------------------------------------------------*\
        | Best-effort release when the object is destroyed.      |
        |                                                       |
        | This is opt-in because `paint` intentionally leaves  |
        | the final frame in place. Daemon/ordinary direct-mode  |
        | callers should enable it; the GUI design mode should   |
        | leave it disabled.                                    |
        \*-----------------------------------------------------*/
        void set_release_on_destroy(bool enabled) { release_on_destroy = enabled; }

        /*-----------------------------------------------------*\
        | How long to wait for the keyboard to acknowledge a      |
        | report. The reference implementation uses 20 ms; lower |
        | it to raise the achievable frame rate in direct mode.  |
        \*-----------------------------------------------------*/
        void set_ack_timeout_ms(int milliseconds);

        /*-----------------------------------------------------*\
        | Whether the keyboard is currently streaming direct     |
        | frames, i.e. the firmware effect is paused.            |
        \*-----------------------------------------------------*/
        bool direct_mode_active() const { return direct_active; }

    private:
        ClaymoreDevice() = default;

        bool send(const protocol::Report& report, std::string& error);
        bool send_and_await(const protocol::Report& report, std::string& error, int timeout_ms = -1);
        bool query(const protocol::Report& request, std::uint8_t* reply, std::size_t reply_length,
                   int timeout_ms, std::string& error);

        std::unique_ptr<HidrawDevice> transport;
        DeviceStatus                  device_status;
        const LayoutVariant*          active_layout = nullptr;
        bool                          direct_active = false;
        bool                          release_on_destroy = false;
        int                           ack_timeout_ms = 20;
    };

    /*---------------------------------------------------------*\
    | Shared by the CLI and GUI: locate a device and open it     |
    | with a readable explanation of anything that went wrong.   |
    \*---------------------------------------------------------*/
    std::unique_ptr<ClaymoreDevice> open_first
        (
        const DetectOptions&  options,
        std::string&          error
        );
}
