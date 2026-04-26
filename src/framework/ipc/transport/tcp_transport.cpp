#include "tcp_transport.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <vector>

namespace ipc
{
namespace
{
Result SocketFailure(const char* operation)
{
    return Result::Failure(std::string(operation) + ": " + std::strerror(errno));
}
}

TcpTransport::~TcpTransport()
{
    Shutdown();
}

Result TcpTransport::Listen(const Endpoint& endpoint)
{
    if (endpoint.host.empty() || endpoint.port == 0)
    {
        return Result::Failure("invalid endpoint");
    }

    std::scoped_lock lock(mMutex);
    if (mListening)
    {
        return Result::Failure("transport is already listening");
    }

    const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        return SocketFailure("socket");
    }

    int reuse = 1;
    if (::setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
    {
        ::close(socket_fd);
        return SocketFailure("setsockopt");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    if (::inet_pton(AF_INET, endpoint.host.c_str(), &address.sin_addr) != 1)
    {
        ::close(socket_fd);
        return Result::Failure("invalid listen host");
    }

    if (::bind(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        ::close(socket_fd);
        return SocketFailure("bind");
    }

    if (::listen(socket_fd, SOMAXCONN) != 0)
    {
        ::close(socket_fd);
        return SocketFailure("listen");
    }

    mListeningEndpoint = endpoint;
    mListenSocket = socket_fd;
    mListening = true;
    mAcceptThread = std::thread(&TcpTransport::AcceptLoop, this);
    return Result::Success();
}

Result TcpTransport::Connect(const Endpoint& endpoint)
{
    if (endpoint.host.empty() || endpoint.port == 0)
    {
        return Result::Failure("invalid endpoint");
    }

    const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        return SocketFailure("socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    if (::inet_pton(AF_INET, endpoint.host.c_str(), &address.sin_addr) != 1)
    {
        ::close(socket_fd);
        return Result::Failure("invalid remote host");
    }

    if (::connect(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        ::close(socket_fd);
        return SocketFailure("connect");
    }

    const ConnectionId connection_id = AddConnection(socket_fd);
    NotifyConnectionEvent(ConnectionEventType::connected, connection_id);
    return Result::Success();
}

Result TcpTransport::Send(const RawFrame& frame)
{
    int socket_fd = -1;
    {
        std::scoped_lock lock(mMutex);
        const auto it = mConnections.find(frame.connection_id);
        if (it == mConnections.end() || it->second->closed.load())
        {
            return Result::Failure("connection not found");
        }
        socket_fd = it->second->socket_fd;
    }

    const auto header_bytes = SerializeFrameHeader(frame.header);
    if (!WriteExact(socket_fd, header_bytes.data(), header_bytes.size()))
    {
        HandleDisconnect(frame.connection_id, true);
        return SocketFailure("send header");
    }
    if (!frame.payload.empty() && !WriteExact(socket_fd, frame.payload.data(), frame.payload.size()))
    {
        HandleDisconnect(frame.connection_id, true);
        return SocketFailure("send payload");
    }
    return Result::Success();
}

Result TcpTransport::Close(const ConnectionId connection_id)
{
    HandleDisconnect(connection_id, true);
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

void TcpTransport::Shutdown()
{
    std::thread accept_thread;
    std::vector<std::pair<ConnectionId, std::thread>> reader_threads;

    {
        std::scoped_lock lock(mMutex);
        if (mListenSocket >= 0)
        {
            ::shutdown(mListenSocket, SHUT_RDWR);
            ::close(mListenSocket);
            mListenSocket = -1;
        }
        mListening = false;
        if (mAcceptThread.joinable())
        {
            accept_thread = std::move(mAcceptThread);
        }

        for (auto& [connection_id, connection] : mConnections)
        {
            connection->closed.store(true);
            if (connection->socket_fd >= 0)
            {
                ::shutdown(connection->socket_fd, SHUT_RDWR);
                ::close(connection->socket_fd);
                connection->socket_fd = -1;
            }
            if (connection->reader_thread.joinable())
            {
                reader_threads.emplace_back(connection_id, std::move(connection->reader_thread));
            }
        }
        mConnections.clear();
    }

    if (accept_thread.joinable())
    {
        accept_thread.join();
    }
    for (auto& [_, reader] : reader_threads)
    {
        if (reader.joinable())
        {
            reader.join();
        }
    }
}

void TcpTransport::AcceptLoop()
{
    while (true)
    {
        int listen_socket = -1;
        {
            std::scoped_lock lock(mMutex);
            if (!mListening || mListenSocket < 0)
            {
                return;
            }
            listen_socket = mListenSocket;
        }

        sockaddr_in address{};
        socklen_t address_length = sizeof(address);
        const int socket_fd = ::accept(listen_socket, reinterpret_cast<sockaddr*>(&address), &address_length);
        if (socket_fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            std::scoped_lock lock(mMutex);
            if (!mListening || mListenSocket < 0)
            {
                return;
            }
            continue;
        }

        const ConnectionId connection_id = AddConnection(socket_fd);
        NotifyConnectionEvent(ConnectionEventType::connected, connection_id);
    }
}

void TcpTransport::ReaderLoop(const ConnectionId connection_id)
{
    while (true)
    {
        int socket_fd = -1;
        {
            std::scoped_lock lock(mMutex);
            const auto it = mConnections.find(connection_id);
            if (it == mConnections.end() || it->second->closed.load())
            {
                return;
            }
            socket_fd = it->second->socket_fd;
        }

        std::array<std::byte, kFrameHeaderSize> header_bytes{};
        if (!ReadExact(socket_fd, header_bytes.data(), header_bytes.size()))
        {
            break;
        }

        FrameHeader header;
        if (!DeserializeFrameHeader(header_bytes.data(), header_bytes.size(), header))
        {
            break;
        }

        ByteBuffer payload(header.length);
        if (header.length > 0 && !ReadExact(socket_fd, payload.data(), payload.size()))
        {
            break;
        }

        FrameHandler handler;
        {
            std::scoped_lock lock(mMutex);
            handler = mFrameHandler;
        }

        if (handler)
        {
            RawFrame frame;
            frame.connection_id = connection_id;
            frame.header = header;
            frame.payload = std::move(payload);
            handler(frame);
        }
    }

    HandleDisconnect(connection_id, true);
}

ConnectionId TcpTransport::AddConnection(const int socket_fd)
{
    std::unique_ptr<Connection> connection = std::make_unique<Connection>();
    connection->socket_fd = socket_fd;

    ConnectionId connection_id = 0;
    {
        std::scoped_lock lock(mMutex);
        connection_id = mNextConnectionId++;
        mConnections.emplace(connection_id, std::move(connection));
    }

    std::thread reader_thread(&TcpTransport::ReaderLoop, this, connection_id);
    {
        std::scoped_lock lock(mMutex);
        const auto it = mConnections.find(connection_id);
        if (it != mConnections.end())
        {
            it->second->reader_thread = std::move(reader_thread);
        }
    }

    return connection_id;
}

void TcpTransport::HandleDisconnect(const ConnectionId connection_id, const bool notify)
{
    std::thread reader_thread;
    bool should_notify = false;

    {
        std::scoped_lock lock(mMutex);
        const auto it = mConnections.find(connection_id);
        if (it == mConnections.end())
        {
            return;
        }

        if (it->second->closed.exchange(true))
        {
            return;
        }

        should_notify = notify;
        if (it->second->socket_fd >= 0)
        {
            ::shutdown(it->second->socket_fd, SHUT_RDWR);
            ::close(it->second->socket_fd);
            it->second->socket_fd = -1;
        }

        if (it->second->reader_thread.joinable())
        {
            if (it->second->reader_thread.get_id() == std::this_thread::get_id())
            {
                it->second->reader_thread.detach();
            }
            else
            {
                reader_thread = std::move(it->second->reader_thread);
            }
        }

        mConnections.erase(it);
    }

    if (reader_thread.joinable())
    {
        reader_thread.join();
    }

    if (should_notify)
    {
        NotifyConnectionEvent(ConnectionEventType::disconnected, connection_id);
    }
}

void TcpTransport::NotifyConnectionEvent(const ConnectionEventType type, const ConnectionId connection_id)
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

bool TcpTransport::ReadExact(const int socket_fd, void* buffer, const std::size_t size) const
{
    auto* cursor = static_cast<std::byte*>(buffer);
    std::size_t read_total = 0;
    while (read_total < size)
    {
        const ssize_t rc = ::recv(socket_fd, cursor + read_total, size - read_total, 0);
        if (rc <= 0)
        {
            return false;
        }
        read_total += static_cast<std::size_t>(rc);
    }
    return true;
}

bool TcpTransport::WriteExact(const int socket_fd, const void* buffer, const std::size_t size) const
{
    const auto* cursor = static_cast<const std::byte*>(buffer);
    std::size_t written_total = 0;
    while (written_total < size)
    {
        const ssize_t rc = ::send(socket_fd, cursor + written_total, size - written_total, 0);
        if (rc <= 0)
        {
            return false;
        }
        written_total += static_cast<std::size_t>(rc);
    }
    return true;
}
} // namespace ipc
