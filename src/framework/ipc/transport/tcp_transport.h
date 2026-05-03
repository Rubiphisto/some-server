#pragma once

#include "transport.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace ipc
{
// TCP transport implementation that owns sockets, accept loop, and frame IO.
class TcpTransport final : public ITransport
{
public:
    ~TcpTransport() override;

    // Binds and starts the TCP listener used for inbound IPC links.
    Result Listen(const Endpoint& endpoint) override;
    // Opens one outbound TCP connection to a remote IPC endpoint.
    Result Connect(const Endpoint& endpoint) override;
    // Writes one complete IPC frame to the target connection.
    Result Send(const RawFrame& frame) override;
    // Closes the selected TCP connection and notifies upper layers.
    Result Close(ConnectionId connection_id) override;
    // Sets the callback for successfully decoded inbound frames.
    void SetFrameHandler(FrameHandler handler) override;
    // Sets the callback for transport connect/disconnect events.
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
