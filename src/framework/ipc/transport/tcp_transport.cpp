#include "tcp_transport.h"

namespace ipc
{
Result TcpTransport::Listen(const Endpoint& endpoint)
{
    std::scoped_lock lock(mMutex);
    mListeningEndpoint = endpoint;
    mListening = true;
    return Result::Success();
}

Result TcpTransport::Connect(const Endpoint& endpoint)
{
    if (endpoint.host.empty() || endpoint.port == 0)
    {
        return Result::Failure("invalid endpoint");
    }

    ConnectionId connection_id = 0;
    {
        std::scoped_lock lock(mMutex);
        connection_id = mNextConnectionId++;
        mConnections.insert(connection_id);
    }
    // Transport is still a loopback test stub in milestone 1. It raises synthetic
    // connection events so link-layer logic can be built and verified first.
    NotifyConnectionEvent(ConnectionEventType::connected, connection_id);
    return Result::Success();
}

Result TcpTransport::Send(const RawFrame& frame)
{
    FrameHandler handler;
    {
        std::scoped_lock lock(mMutex);
        if (!mConnections.contains(frame.connection_id))
        {
            return Result::Failure("connection not found");
        }
        handler = mFrameHandler;
    }

    if (handler)
    {
        // For now send() feeds the frame straight back into the registered handler.
        // Real socket IO will replace this path when transport moves beyond the stub.
        handler(frame);
    }
    return Result::Success();
}

Result TcpTransport::Close(ConnectionId connection_id)
{
    bool removed = false;
    {
        std::scoped_lock lock(mMutex);
        removed = mConnections.erase(connection_id) > 0;
    }
    if (!removed)
    {
        return Result::Failure("connection not found");
    }

    NotifyConnectionEvent(ConnectionEventType::disconnected, connection_id);
    return Result::Success();
}

void TcpTransport::SetFrameHandler(FrameHandler handler)
{
    std::scoped_lock lock(mMutex);
    mFrameHandler = std::move(handler);
}

void TcpTransport::SetConnectionEventHandler(ConnectionEventHandler handler)
{
    std::scoped_lock lock(mMutex);
    mConnectionEventHandler = std::move(handler);
}

void TcpTransport::NotifyConnectionEvent(ConnectionEventType type, ConnectionId connection_id)
{
    ConnectionEventHandler handler;
    {
        std::scoped_lock lock(mMutex);
        handler = mConnectionEventHandler;
    }

    if (handler)
    {
        handler(ConnectionEvent{type, connection_id});
    }
}
} // namespace ipc
