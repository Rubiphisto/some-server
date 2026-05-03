#pragma once

#include "../base/process.h"
#include "../transport/transport.h"

namespace ipc
{
// Tracks handshake progress and final health for one process link.
enum class LinkState : std::uint8_t
{
    idle,
    handshaking,
    active,
    closed
};

// Runtime state for one remote process link bound to one TCP connection.
struct Link
{
    ConnectionId connection_id = 0;
    ProcessRef remote_process;
    LinkState state = LinkState::idle;
};
} // namespace ipc
