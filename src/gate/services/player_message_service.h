#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/envelope.h"
#include "../../framework/ipc/base/result.h"

#include <ipc/gate_game/v1/player_message.pb.h>
#include <ipc/gate_game/v1/push.pb.h>
#include <message_ids.pb.h>
#include <player.pb.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

class GateConnectionService;
class GateIpcService;
class GateClientProtocolService;
class GateSessionService;

class GatePlayerMessageService final : public ServiceBase
{
public:
    GatePlayerMessageService(
        GateConnectionService* connection_service,
        GateSessionService* session_service,
        GateIpcService* ipc_service,
        GateClientProtocolService* protocol_service);

    ipc::Result HandleClientPlayerMessage(
        std::uint64_t connection_id,
        const pb::PlayerMessageRequest& request);

private:
    void RegisterProtocolHandlers();
    void RegisterProcessHandlers();
    ipc::DispatchResult HandleForwardPlayerMessageResponse(
        const ipc::Envelope& envelope,
        const some_server::ipc::gate_game::v1::ForwardPlayerMessageResponse& response);
    ipc::DispatchResult HandlePushPlayerMessage(
        const ipc::Envelope& envelope,
        const some_server::ipc::gate_game::v1::PushPlayerMessage& push);
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
    GateClientProtocolService* mProtocolService = nullptr;
    std::uint64_t mNextRequestId = 1;
    std::mutex mMutex;
    std::unordered_map<std::uint64_t, PendingMessage> mPendingMessages;
};
