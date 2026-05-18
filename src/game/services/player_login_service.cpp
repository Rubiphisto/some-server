#include "player_login_service.h"

#include "ipc_client_service.h"
#include "player_directory_service.h"
#include "player_session_service.h"

#include <ipc/common.pb.h>
#include <ipc/login.pb.h>
#include <ipc/session.pb.h>

namespace
{
pb::ipc::ResultCode ToResultCode(const ipc::Result& result)
{
    if (result.message == "player lease is already held by another game")
    {
        return pb::ipc::RESULT_CODE_PLAYER_HELD_BY_OTHER_GAME;
    }
    return pb::ipc::RESULT_CODE_INTERNAL;
}
}  // namespace

PlayerLoginService::PlayerLoginService(
    PlayerDirectoryService* directory_service,
    PlayerSessionService* session_service,
    GameIpcClientService* ipc_service)
    : ServiceBase("player_login", 40)
    , mDirectoryService(directory_service)
    , mSessionService(session_service)
    , mIpcService(ipc_service)
{
    RegisterProcessHandlers();
}

void PlayerLoginService::RegisterProcessHandlers()
{
    if (mIpcService == nullptr)
    {
        return;
    }
    mIpcService->RegisterProcessHandler<pb::ipc::LoginPlayerRequest>(
        this,
        &PlayerLoginService::HandleLoginRequest);
    mIpcService->RegisterProcessHandler<pb::ipc::ReconnectPlayerRequest>(
        this,
        &PlayerLoginService::HandleReconnectRequest);
    mIpcService->RegisterProcessHandler<pb::ipc::PlayerDisconnected>(
        this,
        &PlayerLoginService::HandlePlayerDisconnected);
}

ipc::DispatchResult PlayerLoginService::HandleLoginRequest(
    const ipc::Envelope& envelope,
    const pb::ipc::LoginPlayerRequest& request)
{
    if (mDirectoryService == nullptr || mSessionService == nullptr || mIpcService == nullptr)
    {
        return ipc::DispatchResult::Failure("player login dependencies are not registered");
    }

    const auto resolved =
        mDirectoryService->ResolveOrCreate(request.platform(), request.account_id(), request.area_id());
    const auto activate = mSessionService->ActivatePlayer(
        resolved.player_id,
        request.gate_service_type(),
        request.gate_instance_id(),
        request.gate_session_id(),
        resolved.created);

    pb::ipc::LoginPlayerResponse response;
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
        response.set_result_code(pb::ipc::RESULT_CODE_OK);
    }

    const auto send =
        mIpcService->SendProcessPayload(envelope.header.source_process.process_id, response);
    if (!send.ok)
    {
        return ipc::DispatchResult::Failure(send.message);
    }
    return ipc::DispatchResult::Success();
}

ipc::DispatchResult PlayerLoginService::HandleReconnectRequest(
    const ipc::Envelope& envelope,
    const pb::ipc::ReconnectPlayerRequest& request)
{
    if (mSessionService == nullptr || mIpcService == nullptr)
    {
        return ipc::DispatchResult::Failure("player login dependencies are not registered");
    }

    const auto activate = mSessionService->ActivatePlayer(
        request.player_id(),
        request.gate_service_type(),
        request.gate_instance_id(),
        request.gate_session_id());

    pb::ipc::ReconnectPlayerResponse response;
    response.set_request_id(request.request_id());
    response.set_player_id(request.player_id());
    if (!activate.ok)
    {
        response.set_result_code(ToResultCode(activate));
        response.set_error_message(activate.message);
    }
    else
    {
        response.set_result_code(pb::ipc::RESULT_CODE_OK);
    }

    const auto send =
        mIpcService->SendProcessPayload(envelope.header.source_process.process_id, response);
    if (!send.ok)
    {
        return ipc::DispatchResult::Failure(send.message);
    }
    return ipc::DispatchResult::Success();
}

ipc::DispatchResult PlayerLoginService::HandlePlayerDisconnected(
    const ipc::Envelope&,
    const pb::ipc::PlayerDisconnected& request)
{
    if (mSessionService == nullptr)
    {
        return ipc::DispatchResult::Failure("player session service is not registered");
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
