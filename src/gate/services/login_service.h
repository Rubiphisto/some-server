#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/envelope.h"
#include "../../framework/ipc/base/result.h"

#include <common.pb.h>
#include <ipc/gate_game/v1/session.pb.h>

#include "session_service.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

class GateAuthService;
class GateAccountDirectoryService;
class GateConnectionService;
class GateIpcService;
class GateProtocolService;
class GateRoutingService;
class GateSessionService;

struct GateLoginSnapshot
{
    std::uint64_t last_request_id = 0;
    std::uint64_t last_player_id = 0;
    std::uint32_t last_result_code = 0;
    std::uint32_t last_game_service_type = 0;
    std::uint32_t last_game_instance_id = 0;
    bool last_is_reconnect = false;
    std::string last_error_message;
};

class GateLoginService final : public ServiceBase
{
public:
    GateLoginService(
        std::uint32_t gate_instance_id,
        GateIpcService* ipc_service,
        GateAccountDirectoryService* account_directory_service,
        GateConnectionService* connection_service,
        GateSessionService* session_service,
        GateRoutingService* routing_service,
        GateProtocolService* protocol_service,
        GateAuthService* auth_service)
        : ServiceBase("gate_login", 60)
        , mGateInstanceId(gate_instance_id)
        , mIpcService(ipc_service)
        , mAccountDirectoryService(account_directory_service)
        , mConnectionService(connection_service)
        , mSessionService(session_service)
        , mRoutingService(routing_service)
        , mProtocolService(protocol_service)
        , mAuthService(auth_service)
    {
    }

    ipc::Result HandleClientLogin(std::uint64_t connection_id, const std::string& payload);
    ipc::Result HandleSessionDisconnected(
        const GateSessionRecord& session,
        some_server::ipc::gate_game::v1::DisconnectReason reason);
    ipc::Result RequestLogin(
        std::uint32_t game_instance_id,
        std::uint64_t connection_id,
        std::uint64_t gate_session_id,
        std::string_view platform,
        std::string_view account_id,
        std::uint32_t area_id);
    ipc::DispatchResult HandleProcessEnvelope(const ipc::ReceiverAddress& target, const ipc::Envelope& envelope);
    GateLoginSnapshot Snapshot() const { return mSnapshot; }

private:
    ipc::Result KickExistingAccountSession(std::string_view account_id, std::uint64_t current_connection_id);
    ipc::Result HandleKickAccountSession(const ipc::Envelope& envelope);
    ipc::Result HandleUnbindPlayerSession(const ipc::Envelope& envelope);
    struct PendingLogin
    {
        std::uint64_t connection_id = 0;
        std::uint64_t gate_session_id = 0;
        std::string account_id;
    };

    ipc::Result SendLoginFailure(
        std::uint64_t connection_id,
        client::common::v1::ErrorCode error_code,
        std::string_view error_message) const;

    std::uint32_t mGateInstanceId = 1;
    GateIpcService* mIpcService = nullptr;
    GateAccountDirectoryService* mAccountDirectoryService = nullptr;
    GateConnectionService* mConnectionService = nullptr;
    GateSessionService* mSessionService = nullptr;
    GateRoutingService* mRoutingService = nullptr;
    GateProtocolService* mProtocolService = nullptr;
    GateAuthService* mAuthService = nullptr;
    mutable std::mutex mMutex;
    std::unordered_map<std::uint64_t, PendingLogin> mPendingLogins;
    GateLoginSnapshot mSnapshot;
    std::uint64_t mNextRequestId = 1;
};
