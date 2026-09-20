/*---------------------------------------------------------*\
| hidraw.cpp - minimal Linux hidraw transport               |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "claymore/hidraw.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sstream>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/hidraw.h>

namespace claymore
{
    namespace
    {
        std::string trim_newline(std::string text)
        {
            while(!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '\0'))
            {
                text.pop_back();
            }
            return text;
        }

        std::optional<std::string> read_file(const std::string& path)
        {
            const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if(fd < 0)
            {
                return std::nullopt;
            }

            std::string out;
            char        buffer[1024];
            ssize_t     got;
            while((got = ::read(fd, buffer, sizeof(buffer))) > 0)
            {
                out.append(buffer, static_cast<std::size_t>(got));
            }
            ::close(fd);

            if(got < 0)
            {
                return std::nullopt;
            }
            return trim_newline(out);
        }

        std::optional<std::string> sysfs_attr(const std::string& directory, const std::string& attribute)
        {
            return read_file(directory + "/" + attribute);
        }

        std::optional<std::uint32_t> parse_hex(const std::optional<std::string>& text)
        {
            if(!text || text->empty())
            {
                return std::nullopt;
            }
            char*      end   = nullptr;
            const long value = std::strtol(text->c_str(), &end, 16);
            if(end == text->c_str())
            {
                return std::nullopt;
            }
            return static_cast<std::uint32_t>(value);
        }

        std::string parent_dir(const std::string& path)
        {
            const std::size_t slash = path.find_last_of('/');
            return (slash == std::string::npos) ? path : path.substr(0, slash);
        }

        /*-----------------------------------------------------*\
        | Reads the HID report descriptor through the kernel's   |
        | ioctl interface.                                       |
        \*-----------------------------------------------------*/
        std::vector<std::uint8_t> read_report_descriptor(int fd)
        {
            std::vector<std::uint8_t> descriptor;

            int size = 0;
            if(::ioctl(fd, HIDIOCGRDESCSIZE, &size) < 0 || size <= 0)
            {
                return descriptor;
            }

            hidraw_report_descriptor raw;
            std::memset(&raw, 0, sizeof(raw));
            raw.size = static_cast<__u32>(size);
            if(::ioctl(fd, HIDIOCGRDESC, &raw) < 0)
            {
                return descriptor;
            }

            const std::size_t length = std::min<std::size_t>(raw.size, HID_MAX_DESCRIPTOR_SIZE);
            descriptor.assign(raw.value, raw.value + length);
            return descriptor;
        }

        /*-----------------------------------------------------*\
        | HID item helpers, matching the logic hidapi uses so    |
        | that usage_page comes out identical.                   |
        \*-----------------------------------------------------*/
        bool hid_item_size(const std::vector<std::uint8_t>& desc, std::size_t pos, int& data_len, int& key_size)
        {
            if(pos >= desc.size())
            {
                return false;
            }

            const std::uint8_t key = desc[pos];

            if(key == 0xFE) /* Long item */
            {
                if(pos + 2 >= desc.size())
                {
                    return false;
                }
                data_len = desc[pos + 1];
                key_size = 3;
                return true;
            }

            const int size_code = key & 0x03;
            data_len = (size_code == 3) ? 4 : size_code;
            key_size = 1;
            return true;
        }

        std::uint32_t hid_report_bytes(const std::vector<std::uint8_t>& desc, std::size_t pos, int len)
        {
            std::uint32_t value = 0;
            for(int i = 0; i < len; i++)
            {
                if(pos + static_cast<std::size_t>(i) >= desc.size())
                {
                    break;
                }
                value |= static_cast<std::uint32_t>(desc[pos + static_cast<std::size_t>(i)]) << (8 * i);
            }
            return value;
        }

        /*-----------------------------------------------------*\
        | Usage page of the first top level application          |
        | collection, which is what hidapi reports as            |
        | hid_device_info::usage_page.                           |
        \*-----------------------------------------------------*/
        std::uint16_t first_usage_page(const std::vector<std::uint8_t>& desc)
        {
            std::size_t  pos             = 0;
            bool         usage_page_seen = false;
            bool         usage_seen      = false;
            std::uint16_t usage_page     = 0;

            while(pos < desc.size())
            {
                const std::uint8_t key     = desc[pos];
                const std::uint8_t key_cmd = key & 0xFC;

                int data_len = 0;
                int key_size = 0;
                if(!hid_item_size(desc, pos, data_len, key_size))
                {
                    break;
                }

                if(key_cmd == 0x04) /* Usage Page (global) */
                {
                    usage_page      = static_cast<std::uint16_t>(
                        hid_report_bytes(desc, pos + static_cast<std::size_t>(key_size), data_len));
                    usage_page_seen = true;
                }
                else if(key_cmd == 0x08) /* Usage (local) */
                {
                    if(data_len == 4)
                    {
                        usage_page = static_cast<std::uint16_t>(
                            hid_report_bytes(desc, pos + static_cast<std::size_t>(key_size) + 2, 2));
                        usage_page_seen = true;
                    }
                    usage_seen = true;
                }
                else if(key_cmd == 0xA0) /* Collection (main) */
                {
                    if(usage_seen && usage_page_seen)
                    {
                        return usage_page;
                    }
                }

                pos += static_cast<std::size_t>(data_len + key_size);
            }

            return usage_page_seen ? usage_page : 0;
        }

        /*-----------------------------------------------------*\
        | Walks up the sysfs tree looking for the directory that |
        | carries `attribute`, e.g. bInterfaceNumber.            |
        \*-----------------------------------------------------*/
        std::string find_ancestor_with_attr(const std::string& start, const std::string& attribute, int max_levels)
        {
            std::string current = start;
            for(int level = 0; level < max_levels; level++)
            {
                if(sysfs_attr(current, attribute).has_value())
                {
                    return current;
                }
                const std::string parent = parent_dir(current);
                if(parent == current || parent.empty())
                {
                    break;
                }
                current = parent;
            }
            return {};
        }
    }

    std::vector<HidrawInfo> enumerate_hidraw()
    {
        std::vector<HidrawInfo> devices;

        DIR* directory = ::opendir("/sys/class/hidraw");
        if(directory == nullptr)
        {
            return devices;
        }

        std::vector<std::string> names;
        while(struct dirent* entry = ::readdir(directory))
        {
            const std::string name = entry->d_name;
            if(name.rfind("hidraw", 0) == 0)
            {
                names.push_back(name);
            }
        }
        ::closedir(directory);

        std::sort(names.begin(), names.end());

        for(const std::string& name : names)
        {
            HidrawInfo info;
            info.hidraw_name = name;
            info.device_path = "/dev/" + name;

            const std::string sysfs_dir = "/sys/class/hidraw/" + name;

            /*-------------------------------------------------*\
            | VID/PID from the HID uevent                       |
            \*-------------------------------------------------*/
            const auto uevent = read_file(sysfs_dir + "/device/uevent");
            if(!uevent)
            {
                continue;
            }

            unsigned bus = 0, vid = 0, pid = 0;
            {
                const std::size_t marker = uevent->find("HID_ID=");
                if(marker == std::string::npos)
                {
                    continue;
                }
                const std::string value = uevent->substr(marker + 7);
                if(std::sscanf(value.c_str(), "%x:%x:%x", &bus, &vid, &pid) != 3)
                {
                    continue;
                }
            }
            info.vendor_id  = static_cast<std::uint16_t>(vid);
            info.product_id = static_cast<std::uint16_t>(pid);

            /*-------------------------------------------------*\
            | Resolve the symlink so the USB interface and       |
            | device directories can be walked.                  |
            \*-------------------------------------------------*/
            char resolved[PATH_MAX];
            if(::realpath((sysfs_dir + "/device").c_str(), resolved) != nullptr)
            {
                const std::string hid_dir = resolved;

                const std::string interface_dir = find_ancestor_with_attr(hid_dir, "bInterfaceNumber", 4);
                if(!interface_dir.empty())
                {
                    if(const auto value = parse_hex(sysfs_attr(interface_dir, "bInterfaceNumber")))
                    {
                        info.interface_number = static_cast<int>(*value);
                    }
                }

                const std::string usb_dir = find_ancestor_with_attr(hid_dir, "bcdDevice", 6);
                if(!usb_dir.empty())
                {
                    info.usb_device_path = usb_dir;

                    if(const auto value = parse_hex(sysfs_attr(usb_dir, "bcdDevice")))
                    {
                        info.bcd_device = static_cast<std::uint16_t>(*value);
                    }
                    if(const auto value = sysfs_attr(usb_dir, "manufacturer"))
                    {
                        info.manufacturer = *value;
                    }
                    if(const auto value = sysfs_attr(usb_dir, "product"))
                    {
                        info.product = *value;
                    }

                    /*---------------------------------------------*\
                    | The kernel hides the serial number behind a    |
                    | restrictive permission, and the Claymore does  |
                    | not expose one; fall back to the uevent name.  |
                    \*---------------------------------------------*/
                    if(const auto value = sysfs_attr(usb_dir, "serial"))
                    {
                        info.serial = *value;
                    }
                }

                if(info.product.empty())
                {
                    const std::size_t marker = uevent->find("HID_NAME=");
                    if(marker != std::string::npos)
                    {
                        info.product = uevent->substr(marker + 9);
                    }
                }
            }

            /*-------------------------------------------------*\
            | Usage page needs the descriptor, which needs an    |
            | open file descriptor. Read only on purpose: this   |
            | lets 'claymore list' describe devices the user is  |
            | not permitted to write to, which is exactly the    |
            | state a machine is in before the udev rule lands.  |
            \*-------------------------------------------------*/
            const int fd = ::open(info.device_path.c_str(), O_RDONLY | O_CLOEXEC);
            if(fd >= 0)
            {
                info.usage_page = first_usage_page(read_report_descriptor(fd));
                ::close(fd);
            }

            devices.push_back(info);
        }

        return devices;
    }

    std::string describe(const HidrawInfo& info)
    {
        std::ostringstream out;
        char               ids[16];
        std::snprintf(ids, sizeof(ids), "%04x:%04x", info.vendor_id, info.product_id);

        out << info.device_path << "  " << ids << "  iface " << info.interface_number
            << "  usage 0x" << std::hex << info.usage_page << std::dec
            << "  bcd 0x" << std::hex << info.bcd_device << std::dec;

        if(!info.product.empty())
        {
            out << "  \"" << info.product << "\"";
        }
        return out.str();
    }

    std::unique_ptr<HidrawDevice> HidrawDevice::open(const HidrawInfo& info, std::string& error)
    {
        const int fd = ::open(info.device_path.c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if(fd < 0)
        {
            error = "cannot open " + info.device_path + ": " + std::strerror(errno);
            if(errno == EACCES)
            {
                error += " (install udev/99-rog-claymore.rules or run as root)";
            }
            return nullptr;
        }

        std::unique_ptr<HidrawDevice> device(new HidrawDevice());
        device->fd          = fd;
        device->device_info = info;
        device->unnumbered  = info.unnumbered_writes;
        return device;
    }

    HidrawDevice::~HidrawDevice()
    {
        if(fd >= 0)
        {
            ::close(fd);
        }
    }

    bool HidrawDevice::write_report(const std::uint8_t* data, std::size_t length, std::string& error)
    {
        if(fd < 0)
        {
            error = "device is not open";
            return false;
        }

        ssize_t written = ::write(fd, data, length);

        /*-----------------------------------------------------*\
        | A device without numbered reports expects the report   |
        | payload only, so drop the leading report number byte   |
        | and retry once. hidapi probes the descriptor for this; |
        | probing the write itself is cheaper and cannot be      |
        | wrong.                                                 |
        \*-----------------------------------------------------*/
        if(written < 0 && (errno == EINVAL || errno == EMSGSIZE) && length > 1 && data[0] == 0x00)
        {
            written = ::write(fd, data + 1, length - 1);
            if(written >= 0)
            {
                unnumbered       = true;
                device_info.unnumbered_writes = true;
            }
        }

        if(written < 0)
        {
            error = std::string("write failed: ") + std::strerror(errno);
            return false;
        }
        if(static_cast<std::size_t>(written) != length && !unnumbered)
        {
            error = "short write (" + std::to_string(written) + " of " +
                    std::to_string(length) + " bytes)";
            return false;
        }
        return true;
    }

    int HidrawDevice::read_report(std::uint8_t* data, std::size_t length, int timeout_ms, std::string* error)
    {
        if(fd < 0)
        {
            if(error) *error = "device is not open";
            return -1;
        }

        struct pollfd pfd;
        pfd.fd      = fd;
        pfd.events  = POLLIN;
        pfd.revents = 0;

        const int ready = ::poll(&pfd, 1, timeout_ms);
        if(ready == 0)
        {
            return 0; /* timeout */
        }
        if(ready < 0)
        {
            if(errno == EINTR)
            {
                return 0;
            }
            if(error) *error = std::string("poll failed: ") + std::strerror(errno);
            return -1;
        }
        if(pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            if(error) *error = "device disconnected";
            return -1;
        }

        const ssize_t got = ::read(fd, data, length);
        if(got < 0)
        {
            if(errno == EAGAIN || errno == EINPROGRESS)
            {
                return 0;
            }
            if(error) *error = std::string("read failed: ") + std::strerror(errno);
            return -1;
        }
        return static_cast<int>(got);
    }

    void HidrawDevice::flush()
    {
        std::uint8_t discard[128];
        for(int i = 0; i < 64; i++)
        {
            if(read_report(discard, sizeof(discard), 0) <= 0)
            {
                break;
            }
        }
    }
}
