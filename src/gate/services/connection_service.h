#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/result.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

enum class GateConnectionState
{
    accepted,
    handshaking,
    anonymous,
    authenticating,
    bound,
    closing,
    closed,
};

const char* ToString(GateConnectionState state);

struct GateConnectionRecord
{
    GateConnectionState state = GateConnectionState::accepted;
    std::string remote_endpoint;
    std::uint64_t last_recv_time_ms = 0;
    std::uint64_t last_send_time_ms = 0;
    std::uint64_t heartbeat_deadline_ms = 0;
};

class GateConnectionService final : public ServiceBase
{
public:
    using MessageHandler = std::function<void(std::uint64_t, std::uint32_t, const std::string&)>;
    using DisconnectHandler = std::function<void(std::uint64_t)>;

    GateConnectionService(std::string host, std::uint16_t port)
        : ServiceBase("gate_connection", 10)
        , mListenHost(std::move(host))
        , mListenPort(port)
    {
    }

    LifecycleTask Start() override;
    LifecycleTask Stop() override;

    ipc::Result Accept(std::uint64_t connection_id, std::string_view remote_endpoint);
    ipc::Result MarkAnonymous(std::uint64_t connection_id);
    ipc::Result MarkBound(std::uint64_t connection_id);
    ipc::Result Close(std::uint64_t connection_id);
    ipc::Result Send(std::uint64_t connection_id, std::uint32_t message_id, const std::string& payload);
    void SetMessageHandler(MessageHandler handler);
    void SetDisconnectHandler(DisconnectHandler handler);
    std::optional<GateConnectionRecord> Snapshot(std::uint64_t connection_id) const;

private:
    static std::uint64_t NowMs();
    void WorkerLoop();
    void AcceptReadySocket();
    void HandleReadable(std::uint64_t connection_id, int socket_fd);
    void CloseSocketLocked(std::uint64_t connection_id);
    void FinalizeConnection(std::uint64_t connection_id);

    mutable std::mutex mMutex;
    std::unordered_map<std::uint64_t, GateConnectionRecord> mConnections;
    std::unordered_map<std::uint64_t, int> mSocketByConnectionId;
    std::unordered_map<int, std::uint64_t> mConnectionIdBySocket;
    std::string mListenHost;
    std::uint16_t mListenPort = 0;
    std::uint64_t mNextConnectionId = 1;
    int mListenSocketFd = -1;
    bool mStopping = false;
    MessageHandler mMessageHandler;
    DisconnectHandler mDisconnectHandler;
    std::thread mWorkerThread;
};
