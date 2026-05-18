#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/envelope.h"
#include "../../framework/ipc/base/result.h"

#include <common.pb.h>
#include <ipc/login.pb.h>
#include <ipc/session.pb.h>
#include <login.pb.h>

#include "session_service.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

class GateAuthService;
class GateAccountDirectoryService;
class GateConnectionService;
class GateIpcService;
class GateClientProtocolService;
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
        GateClientProtocolService* protocol_service,
        GateAuthService* auth_service);

    ipc::Result HandleClientLogin(std::uint64_t connection_id, const pb::LoginRequest& request);
    ipc::Result HandleSessionDisconnected(
        const GateSessionRecord& session,
        pb::ipc::DisconnectReason reason);
    ipc::Result RequestLogin(
        std::uint32_t game_instance_id,
        std::uint64_t connection_id,
        std::uint64_t gate_session_id,
        std::string_view platform,
        std::string_view account_id,
        std::uint32_t area_id);
    GateLoginSnapshot Snapshot() const { return mSnapshot; }

private:
    void RegisterProtocolHandlers();
    void RegisterProcessHandlers();
    ipc::DispatchResult HandleLoginResponse(
        const ipc::Envelope& envelope,
        const pb::ipc::LoginPlayerResponse& response);
    ipc::Result KickExistingAccountSession(std::string_view account_id, std::uint64_t current_connection_id);
    ipc::DispatchResult HandleKickAccountSession(
        const ipc::Envelope& envelope,
        const pb::ipc::KickAccountSession& request);
    ipc::DispatchResult HandleUnbindPlayerSession(
        const ipc::Envelope& envelope,
        const pb::ipc::UnbindPlayerSession& request);
    struct PendingLogin
    {
        std::uint64_t connection_id = 0;
        std::uint64_t gate_session_id = 0;
        std::string account_id;
    };

    ipc::Result SendLoginFailure(
        std::uint64_t connection_id,
        pb::ErrorCode error_code,
        std::string_view error_message) const;

    std::uint32_t mGateInstanceId = 1;
    GateIpcService* mIpcService = nullptr;
    GateAccountDirectoryService* mAccountDirectoryService = nullptr;
    GateConnectionService* mConnectionService = nullptr;
    GateSessionService* mSessionService = nullptr;
    GateRoutingService* mRoutingService = nullptr;
    GateClientProtocolService* mProtocolService = nullptr;
    GateAuthService* mAuthService = nullptr;
    mutable std::mutex mMutex;
    std::unordered_map<std::uint64_t, PendingLogin> mPendingLogins;
    GateLoginSnapshot mSnapshot;
    std::uint64_t mNextRequestId = 1;
};
