#include "login_service.h"

#include "account_directory_service.h"
#include "auth_service.h"
#include "connection_service.h"
#include "ipc_service.h"
#include "protocol_service.h"
#include "routing_service.h"
#include "session_service.h"

#include <ctime>

#include <ipc/gate_game/v1/login.pb.h>
#include <ipc/gate_game/v1/session.pb.h>

#include "../../framework/ipc/messaging/payload_registry.h"

namespace
{
constexpr ipc::ServiceType kGameServiceType = 10;
constexpr ipc::ServiceType kGateServiceType = 20;
}

ipc::Result GateLoginService::HandleClientLogin(
    const std::uint64_t connection_id,
    const pb::LoginRequest& request)
{
    if (mAuthService == nullptr || mSessionService == nullptr || mConnectionService == nullptr)
    {
        return ipc::Result::Failure("gate login dependencies are not registered");
    }

    const auto auth = mAuthService->Validate(
        request.platform(),
        request.account_id(),
        request.credential());
    if (!auth.ok)
    {
        return SendLoginFailure(connection_id, pb::ERROR_CODE_UNAUTHORIZED, auth.message);
    }

    if (const auto kick = KickExistingAccountSession(auth.account_id, connection_id); !kick.ok)
    {
        return kick;
    }

    const auto gate_session_id = connection_id;
    if (!mSessionService->Snapshot(gate_session_id).has_value())
    {
        const auto open = mSessionService->OpenAnonymous(gate_session_id, connection_id);
        if (!open.ok)
        {
            return open;
        }
    }

    if (const auto anonymous = mConnectionService->MarkAnonymous(connection_id); !anonymous.ok)
    {
        return anonymous;
    }

    return RequestLogin(1, connection_id, gate_session_id, request.platform(), auth.account_id, 1);
}

ipc::Result GateLoginService::RequestLogin(
    const std::uint32_t game_instance_id,
    const std::uint64_t connection_id,
    const std::uint64_t gate_session_id,
    const std::string_view platform,
    const std::string_view account_id,
    const std::uint32_t area_id)
{
    if (mIpcService == nullptr)
    {
        return ipc::Result::Failure("gate ipc service is not registered");
    }

    some_server::ipc::gate_game::v1::LoginPlayerRequest request;
    request.set_request_id(mNextRequestId++);
    request.set_gate_service_type(kGateServiceType);
    request.set_gate_instance_id(mGateInstanceId);
    request.set_gate_session_id(gate_session_id);
    request.set_platform(std::string{platform});
    request.set_account_id(std::string{account_id});
    request.set_area_id(area_id);
    request.set_client_version(1);
    request.set_channel("runtime");
    request.set_login_token("runtime-login-token");

    const auto send = mIpcService->SendProcessPayload(
        ipc::ProcessId{
            .service_type = kGameServiceType,
            .instance_id = game_instance_id},
        request);
    if (!send.ok)
    {
        return send;
    }

    std::scoped_lock lock(mMutex);
    mPendingLogins[request.request_id()] = PendingLogin{
        .connection_id = connection_id,
        .gate_session_id = gate_session_id,
        .account_id = std::string{account_id}};
    mSnapshot.last_request_id = request.request_id();
    return ipc::Result::Success();
}

ipc::Result GateLoginService::KickExistingAccountSession(
    const std::string_view account_id,
    const std::uint64_t current_connection_id)
{
    if (mSessionService == nullptr || mConnectionService == nullptr)
    {
        return ipc::Result::Failure("gate kick dependencies are not registered");
    }

    GateAccountOwner owner;
    bool has_owner = false;
    if (mAccountDirectoryService != nullptr)
    {
        const auto existing_owner = mAccountDirectoryService->LookupAccount(account_id);
        if (existing_owner.has_value())
        {
            owner = *existing_owner;
            has_owner = true;
        }
    }

    if (!has_owner)
    {
        const auto existing = mSessionService->FindByAccount(account_id);
        if (!existing.has_value() || existing->connection_id == current_connection_id)
        {
            return ipc::Result::Success();
        }

        if (mProtocolService != nullptr)
        {
            const auto kick = mProtocolService->EncodeKickNotification("same account logged in on a new connection");
            if (kick.ok)
            {
                (void)mConnectionService->Send(existing->connection_id, kick.message_id, kick.payload);
            }
        }

        return mConnectionService->Close(existing->connection_id);
    }

    if (owner.gate_instance_id == mGateInstanceId)
    {
        const auto existing = mSessionService->Snapshot(owner.gate_session_id);
        if (!existing.has_value() || existing->connection_id == current_connection_id)
        {
            return ipc::Result::Success();
        }
        if (mProtocolService != nullptr)
        {
            const auto kick = mProtocolService->EncodeKickNotification("same account logged in on a new connection");
            if (kick.ok)
            {
                (void)mConnectionService->Send(existing->connection_id, kick.message_id, kick.payload);
            }
        }
        return mConnectionService->Close(existing->connection_id);
    }

    if (mIpcService == nullptr)
    {
        return ipc::Result::Failure("gate ipc service is not registered");
    }

    some_server::ipc::gate_game::v1::KickAccountSession request;
    request.set_request_id(mNextRequestId++);
    request.set_account_id(std::string{account_id});
    request.set_old_gate_service_type(owner.gate_service_type);
    request.set_old_gate_instance_id(owner.gate_instance_id);
    request.set_old_gate_session_id(owner.gate_session_id);
    request.set_new_gate_service_type(kGateServiceType);
    request.set_new_gate_instance_id(mGateInstanceId);
    request.set_reason("same account logged in on a new connection");

    const auto send = mIpcService->SendProcessPayload(
        ipc::ProcessId{
            .service_type = owner.gate_service_type,
            .instance_id = owner.gate_instance_id},
        request);
    if (!send.ok)
    {
        return send;
    }
    return ipc::Result::Success();
}

ipc::Result GateLoginService::HandleSessionDisconnected(
    const GateSessionRecord& session,
    const some_server::ipc::gate_game::v1::DisconnectReason reason)
{
    if (mIpcService == nullptr)
    {
        return ipc::Result::Failure("gate ipc service is not registered");
    }
    if (session.player_id == 0 || session.game_service_type == 0 || session.game_instance_id == 0)
    {
        return ipc::Result::Success();
    }

    some_server::ipc::gate_game::v1::PlayerDisconnected request;
    request.set_player_id(session.player_id);
    request.set_gate_service_type(kGateServiceType);
    request.set_gate_instance_id(mGateInstanceId);
    request.set_gate_session_id(session.gate_session_id);
    request.set_reason(reason);
    request.set_occurred_at_ms(static_cast<std::uint64_t>(std::time(nullptr)) * 1000);

    const auto send = mIpcService->SendProcessPayload(
        ipc::ProcessId{
            .service_type = session.game_service_type,
            .instance_id = session.game_instance_id},
        request);
    if (!send.ok)
    {
        return send;
    }
    if (mRoutingService != nullptr)
    {
        (void)mRoutingService->UnbindPlayer(session.player_id, session.gate_session_id);
    }
    if (mAccountDirectoryService != nullptr)
    {
        (void)mAccountDirectoryService->RemoveAccountIfMatches(
            session.account_id,
            GateAccountOwner{
                .gate_service_type = kGateServiceType,
                .gate_instance_id = mGateInstanceId,
                .gate_session_id = session.gate_session_id});
    }
    return ipc::Result::Success();
}

ipc::DispatchResult GateLoginService::HandleProcessEnvelope(const ipc::ReceiverAddress&, const ipc::Envelope& envelope)
{
    if (envelope.payload_type_url ==
        ipc::PayloadRegistry::TypeUrlFor(some_server::ipc::gate_game::v1::LoginPlayerResponse{}))
    {
        some_server::ipc::gate_game::v1::LoginPlayerResponse response;
        if (!response.ParseFromArray(
                envelope.payload_bytes.data(),
                static_cast<int>(envelope.payload_bytes.size())))
        {
            return ipc::DispatchResult::Failure("failed to parse LoginPlayerResponse");
        }

        mSnapshot.last_request_id = response.request_id();
        mSnapshot.last_player_id = response.player_id();
        mSnapshot.last_result_code = response.result_code();
        mSnapshot.last_game_service_type = response.game_service_type();
        mSnapshot.last_game_instance_id = response.game_instance_id();
        mSnapshot.last_is_reconnect = response.is_reconnect();
        mSnapshot.last_error_message = response.error_message();

        PendingLogin pending;
        {
            std::scoped_lock lock(mMutex);
            const auto it = mPendingLogins.find(response.request_id());
            if (it == mPendingLogins.end())
            {
                return ipc::DispatchResult::Failure("missing pending login");
            }
            pending = it->second;
            mPendingLogins.erase(it);
        }

        if (response.result_code() == some_server::ipc::gate_game::v1::RESULT_CODE_OK)
        {
            if (mSessionService != nullptr)
            {
                (void)mSessionService->Activate(
                    pending.gate_session_id,
                    pending.account_id,
                    response.player_id(),
                    response.game_service_type(),
                    response.game_instance_id());
            }
            if (mRoutingService != nullptr)
            {
                (void)mRoutingService->BindPlayer(
                    response.player_id(),
                    response.game_service_type(),
                    response.game_instance_id(),
                    pending.gate_session_id);
            }
            if (mConnectionService != nullptr)
            {
                (void)mConnectionService->MarkBound(pending.connection_id);
            }
            if (mAccountDirectoryService != nullptr)
            {
                (void)mAccountDirectoryService->BindAccount(
                    pending.account_id,
                    GateAccountOwner{
                        .gate_service_type = kGateServiceType,
                        .gate_instance_id = mGateInstanceId,
                        .gate_session_id = pending.gate_session_id});
            }
        }

        if (mProtocolService == nullptr || mConnectionService == nullptr)
        {
            return ipc::DispatchResult::Success();
        }

        const auto encoded = mProtocolService->EncodeLoginResponse(
            response.player_id(),
            response.is_reconnect(),
            response.result_code() == some_server::ipc::gate_game::v1::RESULT_CODE_OK
                ? pb::ERROR_CODE_OK
                : pb::ERROR_CODE_INTERNAL,
            response.error_message());
        if (!encoded.ok)
        {
            return ipc::DispatchResult::Failure(encoded.message);
        }

        if (const auto send = mConnectionService->Send(pending.connection_id, encoded.message_id, encoded.payload); !send.ok)
        {
            return ipc::DispatchResult::Failure(send.message);
        }
        return ipc::DispatchResult::Success();
    }
    if (envelope.payload_type_url ==
        ipc::PayloadRegistry::TypeUrlFor(some_server::ipc::gate_game::v1::KickAccountSession{}))
    {
        const auto result = HandleKickAccountSession(envelope);
        return result.ok ? ipc::DispatchResult::Success() : ipc::DispatchResult::Failure(result.message);
    }
    if (envelope.payload_type_url ==
        ipc::PayloadRegistry::TypeUrlFor(some_server::ipc::gate_game::v1::UnbindPlayerSession{}))
    {
        const auto result = HandleUnbindPlayerSession(envelope);
        return result.ok ? ipc::DispatchResult::Success() : ipc::DispatchResult::Failure(result.message);
    }
    return ipc::DispatchResult::Success();
}

ipc::Result GateLoginService::HandleKickAccountSession(const ipc::Envelope& envelope)
{
    if (mSessionService == nullptr || mConnectionService == nullptr)
    {
        return ipc::Result::Failure("gate kick handling dependencies are not registered");
    }

    some_server::ipc::gate_game::v1::KickAccountSession request;
    if (!request.ParseFromArray(
            envelope.payload_bytes.data(),
            static_cast<int>(envelope.payload_bytes.size())))
    {
        return ipc::Result::Failure("failed to parse KickAccountSession");
    }

    const auto session = mSessionService->Snapshot(request.old_gate_session_id());
    if (!session.has_value())
    {
        return ipc::Result::Success();
    }

    if (session->account_id != request.account_id())
    {
        return ipc::Result::Success();
    }

    if (mProtocolService != nullptr)
    {
        const auto kick = mProtocolService->EncodeKickNotification(
            request.reason().empty() ? "same account logged in on a new connection" : request.reason());
        if (kick.ok)
        {
            (void)mConnectionService->Send(session->connection_id, kick.message_id, kick.payload);
        }
    }
    return mConnectionService->Close(session->connection_id);
}

ipc::Result GateLoginService::HandleUnbindPlayerSession(const ipc::Envelope& envelope)
{
    if (mSessionService == nullptr || mConnectionService == nullptr)
    {
        return ipc::Result::Failure("gate unbind handling dependencies are not registered");
    }

    some_server::ipc::gate_game::v1::UnbindPlayerSession request;
    if (!request.ParseFromArray(
            envelope.payload_bytes.data(),
            static_cast<int>(envelope.payload_bytes.size())))
    {
        return ipc::Result::Failure("failed to parse UnbindPlayerSession");
    }

    const auto session = mSessionService->Snapshot(request.gate_session_id());
    if (!session.has_value())
    {
        return ipc::Result::Success();
    }
    if (session->player_id != request.player_id())
    {
        return ipc::Result::Success();
    }

    if (mProtocolService != nullptr)
    {
        const auto kick = mProtocolService->EncodeKickNotification("player session ownership moved");
        if (kick.ok)
        {
            (void)mConnectionService->Send(session->connection_id, kick.message_id, kick.payload);
        }
    }

    const auto close = mConnectionService->Close(session->connection_id);
    if (!close.ok && close.message != "connection does not exist")
    {
        return close;
    }
    return ipc::Result::Success();
}

ipc::Result GateLoginService::SendLoginFailure(
    const std::uint64_t connection_id,
    const pb::ErrorCode error_code,
    const std::string_view error_message) const
{
    if (mProtocolService == nullptr || mConnectionService == nullptr)
    {
        return ipc::Result::Failure("gate login failure path dependencies are not registered");
    }

    const auto encoded = mProtocolService->EncodeLoginResponse(0, false, error_code, error_message);
    if (!encoded.ok)
    {
        return ipc::Result::Failure(encoded.message);
    }
    return mConnectionService->Send(connection_id, encoded.message_id, encoded.payload);
}
