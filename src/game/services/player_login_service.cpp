#include "player_login_service.h"

#include "ipc_client_service.h"
#include "player_directory_service.h"
#include "player_session_service.h"

#include <ipc/gate_game/v1/common.pb.h>
#include <ipc/gate_game/v1/login.pb.h>
#include <ipc/gate_game/v1/session.pb.h>

#include "../../framework/ipc/messaging/payload_registry.h"

namespace
{
some_server::ipc::gate_game::v1::ResultCode ToResultCode(const ipc::Result& result)
{
    if (result.message == "player lease is already held by another game")
    {
        return some_server::ipc::gate_game::v1::RESULT_CODE_PLAYER_HELD_BY_OTHER_GAME;
    }
    return some_server::ipc::gate_game::v1::RESULT_CODE_INTERNAL;
}
}

ipc::DispatchResult PlayerLoginService::HandleProcessEnvelope(
    const ipc::ReceiverAddress&,
    const ipc::Envelope& envelope)
{
    if (envelope.payload_type_url ==
        ipc::PayloadRegistry::TypeUrlFor(some_server::ipc::gate_game::v1::LoginPlayerRequest{}))
    {
        return HandleLoginRequest(envelope);
    }
    if (envelope.payload_type_url ==
        ipc::PayloadRegistry::TypeUrlFor(some_server::ipc::gate_game::v1::ReconnectPlayerRequest{}))
    {
        return HandleReconnectRequest(envelope);
    }
    if (envelope.payload_type_url ==
        ipc::PayloadRegistry::TypeUrlFor(some_server::ipc::gate_game::v1::PlayerDisconnected{}))
    {
        return HandlePlayerDisconnected(envelope);
    }
    return ipc::DispatchResult::Success();
}

ipc::DispatchResult PlayerLoginService::HandleLoginRequest(const ipc::Envelope& envelope)
{
    if (mDirectoryService == nullptr || mSessionService == nullptr || mIpcService == nullptr)
    {
        return ipc::DispatchResult::Failure("player login dependencies are not registered");
    }

    some_server::ipc::gate_game::v1::LoginPlayerRequest request;
    if (!request.ParseFromArray(
            envelope.payload_bytes.data(),
            static_cast<int>(envelope.payload_bytes.size())))
    {
        return ipc::DispatchResult::Failure("failed to parse LoginPlayerRequest");
    }

    const auto resolved =
        mDirectoryService->ResolveOrCreate(request.platform(), request.account_id(), request.area_id());
    const auto activate = mSessionService->ActivatePlayer(
        resolved.player_id,
        request.gate_service_type(),
        request.gate_instance_id(),
        request.gate_session_id(),
        resolved.created);

    some_server::ipc::gate_game::v1::LoginPlayerResponse response;
    response.set_request_id(request.request_id());
    response.set_player_id(resolved.player_id);
    response.set_game_service_type(envelope.header.target_receiver.key_hi);
    response.set_game_instance_id(envelope.header.target_receiver.key_lo);
    response.set_is_reconnect(false);
    if (!activate.ok)
    {
        response.set_result_code(ToResultCode(activate));
        response.set_error_message(activate.message);
    }
    else
    {
        response.set_result_code(some_server::ipc::gate_game::v1::RESULT_CODE_OK);
    }

    const auto send =
        mIpcService->SendProcessPayload(envelope.header.source_process.process_id, response);
    if (!send.ok)
    {
        return ipc::DispatchResult::Failure(send.message);
    }
    return ipc::DispatchResult::Success();
}

ipc::DispatchResult PlayerLoginService::HandleReconnectRequest(const ipc::Envelope& envelope)
{
    if (mSessionService == nullptr || mIpcService == nullptr)
    {
        return ipc::DispatchResult::Failure("player login dependencies are not registered");
    }

    some_server::ipc::gate_game::v1::ReconnectPlayerRequest request;
    if (!request.ParseFromArray(
            envelope.payload_bytes.data(),
            static_cast<int>(envelope.payload_bytes.size())))
    {
        return ipc::DispatchResult::Failure("failed to parse ReconnectPlayerRequest");
    }

    const auto activate = mSessionService->ActivatePlayer(
        request.player_id(),
        request.gate_service_type(),
        request.gate_instance_id(),
        request.gate_session_id());

    some_server::ipc::gate_game::v1::ReconnectPlayerResponse response;
    response.set_request_id(request.request_id());
    response.set_player_id(request.player_id());
    if (!activate.ok)
    {
        response.set_result_code(ToResultCode(activate));
        response.set_error_message(activate.message);
    }
    else
    {
        response.set_result_code(some_server::ipc::gate_game::v1::RESULT_CODE_OK);
    }

    const auto send =
        mIpcService->SendProcessPayload(envelope.header.source_process.process_id, response);
    if (!send.ok)
    {
        return ipc::DispatchResult::Failure(send.message);
    }
    return ipc::DispatchResult::Success();
}

ipc::DispatchResult PlayerLoginService::HandlePlayerDisconnected(const ipc::Envelope& envelope)
{
    if (mSessionService == nullptr)
    {
        return ipc::DispatchResult::Failure("player session service is not registered");
    }

    some_server::ipc::gate_game::v1::PlayerDisconnected request;
    if (!request.ParseFromArray(
            envelope.payload_bytes.data(),
            static_cast<int>(envelope.payload_bytes.size())))
    {
        return ipc::DispatchResult::Failure("failed to parse PlayerDisconnected");
    }

    const auto detach = mSessionService->DetachPlayer(
        request.player_id(),
        request.gate_service_type(),
        request.gate_instance_id(),
        request.gate_session_id());
    if (!detach.ok && detach.message != "player session is not tracked")
    {
        return ipc::DispatchResult::Failure(detach.message);
    }
    return ipc::DispatchResult::Success();
}
