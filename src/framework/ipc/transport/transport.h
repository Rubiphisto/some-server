#pragma once

#include "../base/process.h"
#include "../base/result.h"
#include "frame.h"

#include <functional>

namespace ipc
{
enum class ConnectionEventType : std::uint8_t
{
    connected,
    disconnected
};

struct ConnectionEvent
{
    ConnectionEventType type = ConnectionEventType::connected;
    ConnectionId connection_id = 0;
};

using FrameHandler = std::function<void(const RawFrame&)>;
using ConnectionEventHandler = std::function<void(const ConnectionEvent&)>;

class ITransport
{
public:
    virtual ~ITransport() = default;

    virtual Result Listen(const Endpoint& endpoint) = 0;
    virtual Result Connect(const Endpoint& endpoint) = 0;
    virtual Result Send(const RawFrame& frame) = 0;
    virtual Result Close(ConnectionId connection_id) = 0;
    virtual void SetFrameHandler(FrameHandler handler) = 0;
    virtual void SetConnectionEventHandler(ConnectionEventHandler handler) = 0;
};
} // namespace ipc
