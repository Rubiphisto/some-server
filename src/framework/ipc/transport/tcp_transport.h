#pragma once

#include "transport.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace ipc
{
class TcpTransport final : public ITransport
{
public:
    ~TcpTransport() override;

    Result Listen(const Endpoint& endpoint) override;
    Result Connect(const Endpoint& endpoint) override;
    Result Send(const RawFrame& frame) override;
    Result Close(ConnectionId connection_id) override;
    void SetFrameHandler(FrameHandler handler) override;
    void SetConnectionEventHandler(ConnectionEventHandler handler) override;

private:
    struct Connection
    {
        int socket_fd = -1;
        std::thread reader_thread;
        std::atomic<bool> closed = false;
    };

    void Shutdown();
    void AcceptLoop();
    void ReaderLoop(ConnectionId connection_id);
    ConnectionId AddConnection(int socket_fd);
    void HandleDisconnect(ConnectionId connection_id, bool notify);
    void NotifyConnectionEvent(ConnectionEventType type, ConnectionId connection_id);
    bool ReadExact(int socket_fd, void* buffer, std::size_t size) const;
    bool WriteExact(int socket_fd, const void* buffer, std::size_t size) const;

    std::mutex mMutex;
    Endpoint mListeningEndpoint;
    bool mListening = false;
    int mListenSocket = -1;
    std::thread mAcceptThread;
    std::unordered_map<ConnectionId, std::unique_ptr<Connection>> mConnections;
    ConnectionId mNextConnectionId = 1;
    FrameHandler mFrameHandler;
    ConnectionEventHandler mConnectionEventHandler;
};
} // namespace ipc
