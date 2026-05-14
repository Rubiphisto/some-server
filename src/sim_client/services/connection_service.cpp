#include "connection_service.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

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

ConnectionService::ConnectionService(std::string host, const std::uint32_t port)
    : ServiceBase("sim_client_connection", 10)
{
    mSnapshot.host = std::move(host);
    mSnapshot.port = port;
}

LifecycleTask ConnectionService::Stop()
{
    std::thread reader;
    int socket_fd = -1;
    {
        std::scoped_lock lock(mMutex);
        socket_fd = mSocketFd;
        mSocketFd = -1;
        reader = std::move(mReaderThread);
        mSnapshot.connected = false;
    }

    if (socket_fd >= 0)
    {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
    }
    if (reader.joinable())
    {
        reader.join();
    }
    return LifecycleTask::Completed();
}

ipc::Result ConnectionService::Connect()
{
    std::thread stale_reader;
    {
        std::scoped_lock lock(mMutex);
        ++mSnapshot.connect_attempts;
        if (mSocketFd >= 0)
        {
            return ipc::Result::Failure("socket already connected");
        }
        if (mReaderThread.joinable())
        {
            stale_reader = std::move(mReaderThread);
        }
    }

    if (stale_reader.joinable())
    {
        stale_reader.join();
    }

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        return ipc::Result::Failure("failed to create socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(mSnapshot.port));
    if (inet_pton(AF_INET, mSnapshot.host.c_str(), &address.sin_addr) != 1)
    {
        close(socket_fd);
        return ipc::Result::Failure("failed to parse gate host");
    }
    if (connect(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        close(socket_fd);
        return ipc::Result::Failure("failed to connect to gate");
    }

    {
        std::scoped_lock lock(mMutex);
        mSocketFd = socket_fd;
        mSnapshot.connected = true;
        mSnapshot.last_connect_ms = NowMs();
        mReaderThread = std::thread([this] { ReaderLoop(); });
    }
    return ipc::Result::Success();
}

ipc::Result ConnectionService::Disconnect()
{
    std::thread reader;
    int socket_fd = -1;
    {
        std::scoped_lock lock(mMutex);
        if (mSocketFd < 0)
        {
            return ipc::Result::Failure("socket is not connected");
        }

        socket_fd = mSocketFd;
        mSocketFd = -1;
        reader = std::move(mReaderThread);
        ++mSnapshot.disconnect_count;
        mSnapshot.connected = false;
        mSnapshot.last_disconnect_ms = NowMs();
    }

    shutdown(socket_fd, SHUT_RDWR);
    close(socket_fd);
    if (reader.joinable())
    {
        reader.join();
    }
    return ipc::Result::Success();
}

ipc::Result ConnectionService::SendFrame(const std::uint32_t message_id, const std::string& payload)
{
    std::scoped_lock lock(mMutex);
    if (mSocketFd < 0)
    {
        return ipc::Result::Failure("socket is not connected");
    }

    std::uint32_t header[2] = {
        message_id,
        static_cast<std::uint32_t>(payload.size())};
    if (!WriteExact(mSocketFd, header, sizeof(header)))
    {
        return ipc::Result::Failure("failed to send frame header");
    }
    if (!payload.empty() && !WriteExact(mSocketFd, payload.data(), payload.size()))
    {
        return ipc::Result::Failure("failed to send frame payload");
    }
    ++mSnapshot.sent_frame_count;
    return ipc::Result::Success();
}

void ConnectionService::SetMessageHandler(MessageHandler handler)
{
    std::scoped_lock lock(mMutex);
    mMessageHandler = std::move(handler);
}

SimClientConnectionSnapshot ConnectionService::Snapshot() const
{
    std::scoped_lock lock(mMutex);
    return mSnapshot;
}

std::uint64_t ConnectionService::NowMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

void ConnectionService::ReaderLoop()
{
    while (true)
    {
        int fd = -1;
        MessageHandler handler;
        {
            std::scoped_lock lock(mMutex);
            fd = mSocketFd;
            handler = mMessageHandler;
        }
        if (fd < 0)
        {
            return;
        }

        std::uint32_t header[2] = {};
        if (!ReadExact(fd, header, sizeof(header)))
        {
            break;
        }
        std::string payload(header[1], '\0');
        if (header[1] > 0 && !ReadExact(fd, payload.data(), payload.size()))
        {
            break;
        }

        {
            std::scoped_lock lock(mMutex);
            ++mSnapshot.received_frame_count;
        }
        if (handler)
        {
            handler(header[0], payload);
        }
    }

    std::scoped_lock lock(mMutex);
    if (mSocketFd >= 0)
    {
        close(mSocketFd);
        mSocketFd = -1;
    }
    ++mSnapshot.disconnect_count;
    mSnapshot.connected = false;
    mSnapshot.last_disconnect_ms = NowMs();
}
