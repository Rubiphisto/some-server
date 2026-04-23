#pragma once

#include "../base/process.h"
#include "../transport/transport.h"

namespace ipc
{
enum class LinkState : std::uint8_t
{
    idle,
    handshaking,
    active,
    closed
};

struct Link
{
    ConnectionId connection_id = 0;
    ProcessRef remote_process;
    LinkState state = LinkState::idle;
};
} // namespace ipc
