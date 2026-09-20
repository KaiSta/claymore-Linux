/*---------------------------------------------------------*\
| sync_server.hpp - Aura Sync style colour sharing          |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
|                                                           |
|   On Windows, Aura Sync lets any application stream        |
|   colours to the keyboard through a shared API. This is    |
|   the Linux equivalent: a small daemon holds the keyboard  |
|   in direct mode and any process can push frames at it     |
|   over a UNIX datagram socket.                             |
|                                                           |
|   Wire format, one datagram per frame (all little endian): |
|                                                           |
|     offset 0   4 bytes   magic "RGBC"                      |
|     offset 4   1 byte    version, currently 1              |
|     offset 5   1 byte    flags: bit 0 sparse, bit 1 release |
|     offset 6   2 bytes   entry count                       |
|     offset 8   payload                                     |
|                                                           |
|   Dense payload  : count * 3 bytes, one RGB per frame slot |
|   Sparse payload : count * 4 bytes, slot then RGB          |
|                                                           |
|   A "release" datagram has no payload and asks the daemon  |
|   to stop driving the keyboard so the firmware effect can  |
|   take over again.                                          |
|                                                           |
|   Multiple senders are supported on a last-writer-wins     |
|   basis, which matches how Aura Sync arbitrates between    |
|   applications.                                            |
\*---------------------------------------------------------*/

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "claymore/color.hpp"
#include "claymore/effects.hpp"

namespace claymore
{
    struct SyncUpdate
    {
        int slot = 0; /* frame slot, not device LED index */
        Rgb color;
    };

    struct SyncMessage
    {
        bool                   sparse  = false;
        bool                   release = false;
        std::vector<SyncUpdate> updates;
    };

    constexpr std::size_t SYNC_HEADER_LENGTH = 8;
    constexpr std::uint8_t SYNC_VERSION      = 1;

    std::vector<std::uint8_t> sync_encode(const SyncMessage& message);
    bool                      sync_decode(const std::uint8_t* data, std::size_t length,
                                           SyncMessage& out, std::string& error);

    /*---------------------------------------------------------*\
    | Default socket location: $XDG_RUNTIME_DIR/rog-claymore.sock |
    | falling back to /tmp/rog-claymore-$UID.sock.                |
    \*---------------------------------------------------------*/
    std::string default_socket_path();

    class SyncServer
    {
    public:
        /*-----------------------------------------------------*\
        | `led_count` is the number of frame slots of the layout |
        | in use, which bounds and validates incoming updates.   |
        \*-----------------------------------------------------*/
        static std::unique_ptr<SyncServer> create
            (
            const std::string& socket_path,
            int                led_count,
            std::string&       error
            );

        ~SyncServer();

        SyncServer(const SyncServer&)            = delete;
        SyncServer& operator=(const SyncServer&) = delete;

        /*-----------------------------------------------------*\
        | Waits up to `timeout_ms` for datagrams and folds them  |
        | into the current frame. Returns the number of messages |
        | applied (0 on timeout).                                |
        \*-----------------------------------------------------*/
        int poll(int timeout_ms, std::string& error);

        /*-----------------------------------------------------*\
        | Latest composed frame.                                |
        \*-----------------------------------------------------*/
        const Frame& frame() const { return current_frame; }

        /*-----------------------------------------------------*\
        | True when at least one sender has pushed a frame since |
        | the last release.                                      |
        \*-----------------------------------------------------*/
        bool is_driving() const { return driving; }

        /*-----------------------------------------------------*\
        | Number of datagrams received since creation.           |
        \*-----------------------------------------------------*/
        std::uint64_t message_count() const { return messages_seen; }

        const std::string& path() const { return socket_path; }

    private:
        SyncServer() = default;

        int          fd = -1;
        std::string  socket_path;
        Frame        current_frame;
        bool         driving = false;
        std::uint64_t messages_seen = 0;
    };
}
