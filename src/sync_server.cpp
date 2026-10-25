/*---------------------------------------------------------*\
| sync_server.cpp - Aura Sync style colour sharing          |
|                                                           |
|   Part of Claymore@Linux                                  |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "claymore/sync_server.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace claymore
{
    namespace
    {
        constexpr std::uint16_t MAX_ENTRIES = 4096;
    }

    std::vector<std::uint8_t> sync_encode(const SyncMessage& message)
    {
        std::vector<std::uint8_t> out;
        out.reserve(SYNC_HEADER_LENGTH + message.updates.size() * (message.sparse ? 4 : 3));

        if(message.updates.size() > 0xFFFF)
        {
            return {};
        }

        if(!message.release && message.updates.empty())
        {
            /*-------------------------------------------------*\
            | An empty non-release datagram carries no state change.  |
            | Treat it as invalid so callers do not mistake a     |
            | truncated or empty frame for a successful update.   |
            \*-------------------------------------------------*/
            return {};
        }
        if(message.sparse)
        {
            for(const SyncUpdate& update : message.updates)
            {
                if(update.slot < 0 || update.slot > SYNC_MAX_SLOT)
                {
                    return {};
                }
            }
        }

        out.push_back('R');
        out.push_back('G');
        out.push_back('B');
        out.push_back('C');
        out.push_back(SYNC_VERSION);

        std::uint8_t flags = 0;
        if(message.sparse)  flags |= 0x01;
        if(message.release) flags |= 0x02;
        out.push_back(flags);

        const std::uint16_t count = static_cast<std::uint16_t>(message.updates.size());
        out.push_back(static_cast<std::uint8_t>(count & 0xFF));
        out.push_back(static_cast<std::uint8_t>((count >> 8) & 0xFF));

        for(const SyncUpdate& update : message.updates)
        {
            if(message.sparse)
            {
                out.push_back(static_cast<std::uint8_t>(update.slot));
            }
            out.push_back(update.color.r);
            out.push_back(update.color.g);
            out.push_back(update.color.b);
        }

        return out;
    }

    bool sync_decode(const std::uint8_t* data, std::size_t length, SyncMessage& out, std::string& error)
    {
        if(data == nullptr || length < SYNC_HEADER_LENGTH)
        {
            error = "datagram shorter than the 8 byte header";
            return false;
        }

        if(data[0] != 'R' || data[1] != 'G' || data[2] != 'B' || data[3] != 'C')
        {
            error = "bad magic, expected \"RGBC\"";
            return false;
        }

        if(data[4] != SYNC_VERSION)
        {
            error = "unsupported sync protocol version " + std::to_string(data[4]);
            return false;
        }

        const std::uint8_t flags = data[5];
        const std::uint16_t count = static_cast<std::uint16_t>(data[6] | (data[7] << 8));

        out.sparse  = (flags & 0x01) != 0;
        out.release = (flags & 0x02) != 0;
        out.updates.clear();

        if(out.release)
        {
            return true;
        }

        if(count > MAX_ENTRIES)
        {
            error = "absurd entry count " + std::to_string(count);
            return false;
        }

        const std::size_t stride = out.sparse ? 4 : 3;
        const std::size_t needed = SYNC_HEADER_LENGTH + static_cast<std::size_t>(count) * stride;

        if(length < needed)
        {
            error = "datagram truncated: got " + std::to_string(length) +
                    " bytes, need " + std::to_string(needed);
            return false;
        }

        out.updates.reserve(count);
        for(std::size_t i = 0; i < count; i++)
        {
            const std::uint8_t* entry = data + SYNC_HEADER_LENGTH + i * stride;

            SyncUpdate update;
            if(out.sparse)
            {
                update.slot = entry[0];
                update.color = Rgb(entry[1], entry[2], entry[3]);
            }
            else
            {
                update.slot = static_cast<int>(i);
                update.color = Rgb(entry[0], entry[1], entry[2]);
            }
            out.updates.push_back(update);
        }

        return true;
    }

    std::string default_socket_path()
    {
        if(const char* runtime = std::getenv("XDG_RUNTIME_DIR"))
        {
            if(*runtime != '\0')
            {
                return std::string(runtime) + "/claymore-linux.sock";
            }
        }
        return "/tmp/claymore-linux-" + std::to_string(::getuid()) + ".sock";
    }

    std::unique_ptr<SyncServer> SyncServer::create
        (
        const std::string& socket_path_wanted,
        int                led_count,
        std::string&       error
        )
    {
        if(led_count <= 0)
        {
            error = "sync server needs a positive LED count";
            return nullptr;
        }

        if(socket_path_wanted.size() + 1 > sizeof(sockaddr_un::sun_path))
        {
            error = "socket path is too long for AF_UNIX: " + socket_path_wanted;
            return nullptr;
        }

        const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if(fd < 0)
        {
            error = std::string("socket() failed: ") + std::strerror(errno);
            return nullptr;
        }

        /*-----------------------------------------------------*\
        | A stale socket from a crashed daemon may be replaced,   |
        | but never remove a live server's socket.              |
        \*-----------------------------------------------------*/
        struct stat existing;
        if(::lstat(socket_path_wanted.c_str(), &existing) == 0)
        {
            if(!S_ISSOCK(existing.st_mode))
            {
                error = "refusing to replace non-socket path " + socket_path_wanted;
                ::close(fd);
                return nullptr;
            }

            const int probe = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            if(probe >= 0)
            {
                sockaddr_un probe_address;
                std::memset(&probe_address, 0, sizeof(probe_address));
                probe_address.sun_family = AF_UNIX;
                std::snprintf(probe_address.sun_path, sizeof(probe_address.sun_path),
                              "%s", socket_path_wanted.c_str());
                const bool live = ::connect(probe, reinterpret_cast<sockaddr*>(&probe_address),
                                            sizeof(probe_address)) == 0;
                ::close(probe);
                if(live)
                {
                    error = "sync socket is already in use: " + socket_path_wanted;
                    ::close(fd);
                    return nullptr;
                }
            }
            ::unlink(socket_path_wanted.c_str());
        }
        else if(errno != ENOENT)
        {
            error = "cannot inspect socket path " + socket_path_wanted + ": " + std::strerror(errno);
            ::close(fd);
            return nullptr;
        }

        sockaddr_un address;
        std::memset(&address, 0, sizeof(address));
        address.sun_family = AF_UNIX;
        std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path_wanted.c_str());

        if(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
        {
            error = std::string("bind(") + socket_path_wanted + ") failed: " + std::strerror(errno);
            ::close(fd);
            return nullptr;
        }

        /*-----------------------------------------------------*\
        | Owner only: the frames describe what is on your desk.   |
        \*-----------------------------------------------------*/
        if(::chmod(socket_path_wanted.c_str(), S_IRUSR | S_IWUSR) < 0)
        {
            error = "cannot secure socket path " + socket_path_wanted + ": " + std::strerror(errno);
            ::close(fd);
            ::unlink(socket_path_wanted.c_str());
            return nullptr;
        }

        std::unique_ptr<SyncServer> server(new SyncServer());
        server->fd = fd;
        server->socket_path = socket_path_wanted;
        server->current_frame.assign(static_cast<std::size_t>(led_count), Rgb(0, 0, 0));

        return server;
    }

    SyncServer::~SyncServer()
    {
        if(fd >= 0)
        {
            ::close(fd);
        }
        if(!socket_path.empty())
        {
            ::unlink(socket_path.c_str());
        }
    }

    int SyncServer::poll(int timeout_ms, std::string& error)
    {
        int applied = 0;

        /*-----------------------------------------------------*\
        | Drain everything queued so a fast sender cannot build  |
        | latency up in the socket buffer.                       |
        \*-----------------------------------------------------*/
        for(int guard = 0; guard < 256; guard++)
        {
            struct pollfd pfd;
            pfd.fd      = fd;
            pfd.events  = POLLIN;
            pfd.revents = 0;

            const int ready = ::poll(&pfd, 1, applied == 0 ? timeout_ms : 0);
            if(ready == 0)
            {
                break;
            }
            if(ready < 0)
            {
                if(errno == EINTR)
                {
                    break;
                }
                error = std::string("poll() failed: ") + std::strerror(errno);
                return -1;
            }
            if(!(pfd.revents & POLLIN))
            {
                break;
            }

            std::uint8_t buffer[8192];
            const ssize_t got = ::recv(fd, buffer, sizeof(buffer), 0);
            if(got <= 0)
            {
                break;
            }

            SyncMessage message;
            std::string decode_error;
            if(!sync_decode(buffer, static_cast<std::size_t>(got), message, decode_error))
            {
                /*---------------------------------------------*\
                | Ignore malformed datagrams: any user can send  |
                | to this socket and one bad client must not     |
                | take the daemon down.                          |
                \*---------------------------------------------*/
                continue;
            }

            messages_seen++;

            if(message.release)
            {
                driving = false;
                applied++;
                continue;
            }

            for(const SyncUpdate& update : message.updates)
            {
                if(update.slot < 0 || static_cast<std::size_t>(update.slot) >= current_frame.size())
                {
                    continue;
                }
                current_frame[static_cast<std::size_t>(update.slot)] = update.color;
            }

            driving = true;
            applied++;
        }

        return applied;
    }
}
