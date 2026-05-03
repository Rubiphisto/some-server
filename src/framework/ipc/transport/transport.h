#pragma once

#include "../base/process.h"
#include "../base/result.h"
#include "frame.h"

#include <functional>

namespace ipc
{
// Reports connection lifecycle transitions from transport up to link management.
enum class ConnectionEventType : std::uint8_t
{
    connected,
    disconnected
};

// One transport connection lifecycle event.
struct ConnectionEvent
{
    ConnectionEventType type = ConnectionEventType::connected;
    ConnectionId connection_id = 0;
};

using FrameHandler = std::function<void(const RawFrame&)>;
using ConnectionEventHandler = std::function<void(const ConnectionEvent&)>;

// Abstract byte-stream transport used by IPC link and messaging layers.
class ITransport
{
public:
    virtual ~ITransport() = default;

    // Starts accepting inbound IPC connections on endpoint.
    virtual Result Listen(const Endpoint& endpoint) = 0;
    // Opens an outbound IPC connection to endpoint.
    virtual Result Connect(const Endpoint& endpoint) = 0;
    // Sends one fully formed frame on an existing connection.
    virtual Result Send(const RawFrame& frame) = 0;
    // Closes one active connection and releases related transport state.
    virtual Result Close(ConnectionId connection_id) = 0;
    // Installs the callback that receives decoded inbound frames.
    virtual void SetFrameHandler(FrameHandler handler) = 0;
    // Installs the callback that receives connect/disconnect events.
    virtual void SetConnectionEventHandler(ConnectionEventHandler handler) = 0;
};
} // namespace ipc
