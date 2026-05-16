#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/envelope.h"
#include "../../framework/ipc/base/result.h"

#include <player.pb.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

class GateConnectionService;
class GateIpcService;
class GateProtocolService;
class GateSessionService;

class GatePlayerMessageService final : public ServiceBase
{
public:
    GatePlayerMessageService(
        GateConnectionService* connection_service,
        GateSessionService* session_service,
        GateIpcService* ipc_service,
        GateProtocolService* protocol_service)
        : ServiceBase("gate_player_message", 70)
        , mConnectionService(connection_service)
        , mSessionService(session_service)
        , mIpcService(ipc_service)
        , mProtocolService(protocol_service)
    {
    }

    ipc::Result HandleClientPlayerMessage(
        std::uint64_t connection_id,
        const pb::PlayerMessageRequest& request);
    ipc::DispatchResult HandleProcessEnvelope(const ipc::ReceiverAddress& target, const ipc::Envelope& envelope);

private:
    struct PendingMessage
    {
        std::uint64_t connection_id = 0;
        std::uint64_t gate_session_id = 0;
        std::uint64_t player_id = 0;
        std::uint32_t client_message_id = 0;
    };

    GateConnectionService* mConnectionService = nullptr;
    GateSessionService* mSessionService = nullptr;
    GateIpcService* mIpcService = nullptr;
    GateProtocolService* mProtocolService = nullptr;
    std::uint64_t mNextRequestId = 1;
    std::mutex mMutex;
    std::unordered_map<std::uint64_t, PendingMessage> mPendingMessages;
};
