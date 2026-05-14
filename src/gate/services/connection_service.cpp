#include "connection_service.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace
{
bool ReadExact(const int fd, void* buffer, std::size_t size)
{
    auto* bytes = static_cast<std::byte*>(buffer);
    std::size_t offset = 0;
    while (offset < size)
    {
        const auto received = recv(fd, bytes + offset, size - offset, 0);
        if (received <= 0)
        {
            return false;
        }
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

bool WriteExact(const int fd, const void* buffer, std::size_t size)
{
    const auto* bytes = static_cast<const std::byte*>(buffer);
    std::size_t offset = 0;
    while (offset < size)
    {
        const auto sent = send(fd, bytes + offset, size - offset, 0);
        if (sent <= 0)
        {
            return false;
        }
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}
}

const char* ToString(const GateConnectionState state)
{
    switch (state)
    {
    case GateConnectionState::accepted:
        return "accepted";
    case GateConnectionState::handshaking:
        return "handshaking";
    case GateConnectionState::anonymous:
        return "anonymous";
    case GateConnectionState::authenticating:
        return "authenticating";
    case GateConnectionState::bound:
        return "bound";
    case GateConnectionState::closing:
        return "closing";
    case GateConnectionState::closed:
        return "closed";
    }
    return "unknown";
}

LifecycleTask GateConnectionService::Start()
{
    std::scoped_lock lock(mMutex);
    if (mWorkerThread.joinable())
    {
        return LifecycleTask::Completed();
    }

    mListenSocketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (mListenSocketFd < 0)
    {
        return LifecycleTask::Completed();
    }

    int reuse_addr = 1;
    setsockopt(mListenSocketFd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(mListenPort);
    if (inet_pton(AF_INET, mListenHost.c_str(), &address.sin_addr) != 1)
    {
        close(mListenSocketFd);
        mListenSocketFd = -1;
        return LifecycleTask::Completed();
    }

    if (bind(mListenSocketFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(mListenSocketFd, SOMAXCONN) != 0)
    {
        close(mListenSocketFd);
        mListenSocketFd = -1;
        return LifecycleTask::Completed();
    }

    mStopping = false;
    mWorkerThread = std::thread([this] { WorkerLoop(); });
    return LifecycleTask::Completed();
}

LifecycleTask GateConnectionService::Stop()
{
    std::thread worker;
    {
        std::scoped_lock lock(mMutex);
        mStopping = true;
        if (mListenSocketFd >= 0)
        {
            shutdown(mListenSocketFd, SHUT_RDWR);
            close(mListenSocketFd);
            mListenSocketFd = -1;
        }
        for (const auto& [connection_id, socket_fd] : mSocketByConnectionId)
        {
            (void)connection_id;
            shutdown(socket_fd, SHUT_RDWR);
            close(socket_fd);
        }
        mSocketByConnectionId.clear();
        mConnectionIdBySocket.clear();
        worker = std::move(mWorkerThread);
    }

    if (worker.joinable())
    {
        worker.join();
    }
    return LifecycleTask::Completed();
}

ipc::Result GateConnectionService::Accept(const std::uint64_t connection_id, const std::string_view remote_endpoint)
{
    std::scoped_lock lock(mMutex);
    if (mConnections.contains(connection_id))
    {
        return ipc::Result::Failure("connection already exists");
    }

    auto& record = mConnections[connection_id];
    record.state = GateConnectionState::accepted;
    record.remote_endpoint = std::string{remote_endpoint};
    record.last_recv_time_ms = NowMs();
    record.last_send_time_ms = record.last_recv_time_ms;
    record.heartbeat_deadline_ms = record.last_recv_time_ms + 30000;
    return ipc::Result::Success();
}

ipc::Result GateConnectionService::MarkAnonymous(const std::uint64_t connection_id)
{
    std::scoped_lock lock(mMutex);
    const auto it = mConnections.find(connection_id);
    if (it == mConnections.end())
    {
        return ipc::Result::Failure("connection does not exist");
    }
    it->second.state = GateConnectionState::anonymous;
    return ipc::Result::Success();
}

ipc::Result GateConnectionService::MarkBound(const std::uint64_t connection_id)
{
    std::scoped_lock lock(mMutex);
    const auto it = mConnections.find(connection_id);
    if (it == mConnections.end())
    {
        return ipc::Result::Failure("connection does not exist");
    }
    it->second.state = GateConnectionState::bound;
    return ipc::Result::Success();
}

ipc::Result GateConnectionService::Close(const std::uint64_t connection_id)
{
    {
        std::scoped_lock lock(mMutex);
        const auto it = mConnections.find(connection_id);
        if (it == mConnections.end())
        {
            return ipc::Result::Failure("connection does not exist");
        }
        it->second.state = GateConnectionState::closed;
        CloseSocketLocked(connection_id);
    }
    FinalizeConnection(connection_id);
    return ipc::Result::Success();
}

ipc::Result GateConnectionService::Send(
    const std::uint64_t connection_id,
    const std::uint32_t message_id,
    const std::string& payload)
{
    int socket_fd = -1;
    {
        std::scoped_lock lock(mMutex);
        const auto it = mSocketByConnectionId.find(connection_id);
        if (it == mSocketByConnectionId.end())
        {
            return ipc::Result::Failure("connection socket does not exist");
        }
        socket_fd = it->second;
    }

    std::uint32_t header[2] = {message_id, static_cast<std::uint32_t>(payload.size())};
    if (!WriteExact(socket_fd, header, sizeof(header)))
    {
        return ipc::Result::Failure("failed to send frame header");
    }
    if (!payload.empty() && !WriteExact(socket_fd, payload.data(), payload.size()))
    {
        return ipc::Result::Failure("failed to send frame payload");
    }

    std::scoped_lock lock(mMutex);
    if (const auto it = mConnections.find(connection_id); it != mConnections.end())
    {
        it->second.last_send_time_ms = NowMs();
    }
    return ipc::Result::Success();
}

void GateConnectionService::SetMessageHandler(MessageHandler handler)
{
    std::scoped_lock lock(mMutex);
    mMessageHandler = std::move(handler);
}

void GateConnectionService::SetDisconnectHandler(DisconnectHandler handler)
{
    std::scoped_lock lock(mMutex);
    mDisconnectHandler = std::move(handler);
}

std::optional<GateConnectionRecord> GateConnectionService::Snapshot(const std::uint64_t connection_id) const
{
    std::scoped_lock lock(mMutex);
    const auto it = mConnections.find(connection_id);
    if (it == mConnections.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::uint64_t GateConnectionService::NowMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

void GateConnectionService::WorkerLoop()
{
    while (true)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);

        int max_fd = -1;
        std::vector<std::pair<std::uint64_t, int>> sockets;
        {
            std::scoped_lock lock(mMutex);
            if (mStopping)
            {
                return;
            }
            if (mListenSocketFd >= 0)
            {
                FD_SET(mListenSocketFd, &read_fds);
                max_fd = mListenSocketFd;
            }
            sockets.reserve(mSocketByConnectionId.size());
            for (const auto& [connection_id, socket_fd] : mSocketByConnectionId)
            {
                sockets.emplace_back(connection_id, socket_fd);
                FD_SET(socket_fd, &read_fds);
                max_fd = std::max(max_fd, socket_fd);
            }
        }

        if (max_fd < 0)
        {
            return;
        }

        timeval timeout{};
        timeout.tv_sec = 1;
        const auto ready = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (ready <= 0)
        {
            continue;
        }

        bool listen_ready = false;
        {
            std::scoped_lock lock(mMutex);
            listen_ready = mListenSocketFd >= 0 && FD_ISSET(mListenSocketFd, &read_fds);
        }
        if (listen_ready)
        {
            AcceptReadySocket();
        }

        for (const auto& [connection_id, socket_fd] : sockets)
        {
            if (FD_ISSET(socket_fd, &read_fds))
            {
                HandleReadable(connection_id, socket_fd);
            }
        }
    }
}

void GateConnectionService::AcceptReadySocket()
{
    sockaddr_in address{};
    socklen_t address_length = sizeof(address);

    std::scoped_lock lock(mMutex);
    if (mListenSocketFd < 0)
    {
        return;
    }

    const int socket_fd = accept(mListenSocketFd, reinterpret_cast<sockaddr*>(&address), &address_length);
    if (socket_fd < 0)
    {
        return;
    }

    const auto connection_id = mNextConnectionId++;
    char host_buffer[INET_ADDRSTRLEN] = {};
    const char* host = inet_ntop(AF_INET, &address.sin_addr, host_buffer, sizeof(host_buffer));
    std::string remote_endpoint = host != nullptr ? std::string{host} : std::string{"unknown"};
    remote_endpoint += ":" + std::to_string(ntohs(address.sin_port));

    auto& record = mConnections[connection_id];
    record.state = GateConnectionState::accepted;
    record.remote_endpoint = std::move(remote_endpoint);
    record.last_recv_time_ms = NowMs();
    record.last_send_time_ms = record.last_recv_time_ms;
    record.heartbeat_deadline_ms = record.last_recv_time_ms + 30000;
    mSocketByConnectionId[connection_id] = socket_fd;
    mConnectionIdBySocket[socket_fd] = connection_id;
}

void GateConnectionService::HandleReadable(const std::uint64_t connection_id, const int socket_fd)
{
    std::uint32_t header[2] = {};
    if (!ReadExact(socket_fd, header, sizeof(header)))
    {
        FinalizeConnection(connection_id);
        return;
    }

    std::string payload(header[1], '\0');
    if (header[1] > 0 && !ReadExact(socket_fd, payload.data(), payload.size()))
    {
        FinalizeConnection(connection_id);
        return;
    }

    MessageHandler handler;
    {
        std::scoped_lock lock(mMutex);
        if (const auto it = mConnections.find(connection_id); it != mConnections.end())
        {
            it->second.last_recv_time_ms = NowMs();
            it->second.heartbeat_deadline_ms = it->second.last_recv_time_ms + 30000;
        }
        handler = mMessageHandler;
    }

    if (handler)
    {
        handler(connection_id, header[0], payload);
    }
}

void GateConnectionService::CloseSocketLocked(const std::uint64_t connection_id)
{
    const auto it = mSocketByConnectionId.find(connection_id);
    if (it == mSocketByConnectionId.end())
    {
        return;
    }

    shutdown(it->second, SHUT_RDWR);
    close(it->second);
    mConnectionIdBySocket.erase(it->second);
    mSocketByConnectionId.erase(it);
}

void GateConnectionService::FinalizeConnection(const std::uint64_t connection_id)
{
    DisconnectHandler handler;
    {
        std::scoped_lock lock(mMutex);
        if (const auto it = mConnections.find(connection_id); it != mConnections.end())
        {
            it->second.state = GateConnectionState::closed;
            it->second.last_recv_time_ms = NowMs();
        }
        CloseSocketLocked(connection_id);
        handler = mDisconnectHandler;
    }

    if (handler)
    {
        handler(connection_id);
    }
}
