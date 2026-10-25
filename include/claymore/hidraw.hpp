/*---------------------------------------------------------*\
| hidraw.hpp - minimal Linux hidraw transport               |
|                                                           |
|   Part of Claymore@Linux                                  |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
|                                                           |
|   Deliberately dependency free: no hidapi, no libusb, no  |
|   libudev. Enumeration reads /sys directly and transfers  |
|   go through /dev/hidrawN, which is exactly what hidapi's |
|   hidraw backend does underneath. Report bytes are        |
|   passed through verbatim, so packet offsets match the    |
|   reference implementation byte for byte.                 |
\*---------------------------------------------------------*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace claymore
{
    struct HidrawInfo
    {
        std::string  hidraw_name;      /* "hidraw3"                          */
        std::string  device_path;      /* "/dev/hidraw3"                     */
        std::string  usb_device_path;  /* sysfs dir of the USB device        */
        std::uint16_t vendor_id  = 0;
        std::uint16_t product_id = 0;
        std::uint16_t bcd_device = 0;
        int           interface_number = -1;
        std::uint16_t usage_page = 0;
        std::string   manufacturer;
        std::string   product;
        std::string   serial;

        /*-----------------------------------------------------*\
        | Set when a 65 byte write was rejected and the device   |
        | turned out to want 64 bytes (no report number byte).   |
        \*-----------------------------------------------------*/
        bool unnumbered_writes = false;
    };

    /*---------------------------------------------------------*\
    | All hidraw nodes currently present, sorted by node name.   |
    \*---------------------------------------------------------*/
    std::vector<HidrawInfo> enumerate_hidraw();

    /*---------------------------------------------------------*\
    | Human readable one line summary of a device.               |
    \*---------------------------------------------------------*/
    std::string describe(const HidrawInfo& info);

    class HidrawDevice
    {
    public:
        static std::unique_ptr<HidrawDevice> open(const HidrawInfo& info, std::string& error);
        ~HidrawDevice();

        HidrawDevice(const HidrawDevice&)            = delete;
        HidrawDevice& operator=(const HidrawDevice&) = delete;

        /*-----------------------------------------------------*\
        | Writes one report. `data[0]` is the report number (0    |
        | for the ASUS Aura interface). `length` is normally 65.  |
        | Returns false and fills `error` on failure.             |
        \*-----------------------------------------------------*/
        bool write_report(const std::uint8_t* data, std::size_t length, std::string& error);

        /*-----------------------------------------------------*\
        | Reads at most `length` bytes into `data`. Returns the   |
        | number of bytes read, 0 on timeout, or -1 on error.     |
        \*-----------------------------------------------------*/
        int read_report(std::uint8_t* data, std::size_t length, int timeout_ms, std::string* error = nullptr);

        /*-----------------------------------------------------*\
        | Discards every reply currently queued by the device.    |
        \*-----------------------------------------------------*/
        void flush();

        const HidrawInfo& info() const { return device_info; }

    private:
        HidrawDevice() = default;

        int                fd = -1;
        HidrawInfo         device_info;
        bool               unnumbered = false;
    };
}
