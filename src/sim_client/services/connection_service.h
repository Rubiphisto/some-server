#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/result.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

struct SimClientConnectionSnapshot
{
    std::string host;
    std::uint32_t port = 0;
    bool connected = false;
    std::uint64_t connect_attempts = 0;
    std::uint64_t disconnect_count = 0;
    std::uint64_t last_connect_ms = 0;
    std::uint64_t last_disconnect_ms = 0;
    std::uint64_t sent_frame_count = 0;
    std::uint64_t received_frame_count = 0;
};

class ConnectionService final : public ServiceBase
{
public:
    using MessageHandler = std::function<void(std::uint32_t, const std::string&)>;

    ConnectionService(std::string host, std::uint32_t port);

    LifecycleTask Stop() override;
    ipc::Result Connect();
    ipc::Result Disconnect();
    ipc::Result SendFrame(std::uint32_t message_id, const std::string& payload);
    void SetMessageHandler(MessageHandler handler);
    SimClientConnectionSnapshot Snapshot() const;

private:
    static std::uint64_t NowMs();
    void ReaderLoop();

    mutable std::mutex mMutex;
    SimClientConnectionSnapshot mSnapshot;
    int mSocketFd = -1;
    MessageHandler mMessageHandler;
    std::thread mReaderThread;
};
