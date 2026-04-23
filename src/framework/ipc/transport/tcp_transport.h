#pragma once

#include "transport.h"

#include <mutex>
#include <unordered_set>

namespace ipc
{
class TcpTransport final : public ITransport
{
public:
    Result Listen(const Endpoint& endpoint) override;
    Result Connect(const Endpoint& endpoint) override;
    Result Send(const RawFrame& frame) override;
    Result Close(ConnectionId connection_id) override;
    void SetFrameHandler(FrameHandler handler) override;
    void SetConnectionEventHandler(ConnectionEventHandler handler) override;

private:
    void NotifyConnectionEvent(ConnectionEventType type, ConnectionId connection_id);

    std::mutex mMutex;
    Endpoint mListeningEndpoint;
    bool mListening = false;
    std::unordered_set<ConnectionId> mConnections;
    ConnectionId mNextConnectionId = 1;
    FrameHandler mFrameHandler;
    ConnectionEventHandler mConnectionEventHandler;
};
} // namespace ipc
