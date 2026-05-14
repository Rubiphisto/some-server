#include "player_message_service.h"

#include "ipc_client_service.h"
#include "player_lease_service.h"
#include "player_repository.h"
#include "player_runtime_service.h"
#include "player_session_service.h"

#include <ipc/gate_game/v1/common.pb.h>
#include <ipc/gate_game/v1/player_message.pb.h>
#include <ipc/gate_game/v1/push.pb.h>

#include "../../framework/ipc/messaging/payload_registry.h"

ipc::DispatchResult GamePlayerMessageService::HandleProcessEnvelope(const ipc::ReceiverAddress&, const ipc::Envelope& envelope)
{
    if (mSessionService == nullptr || mLeaseService == nullptr || mRepository == nullptr || mRuntimeService == nullptr || mIpcService == nullptr)
    {
        return ipc::DispatchResult::Failure("game player message dependencies are not registered");
    }

    if (envelope.payload_type_url !=
        ipc::PayloadRegistry::TypeUrlFor(some_server::ipc::gate_game::v1::ForwardPlayerMessageRequest{}))
    {
        return ipc::DispatchResult::Success();
    }

    some_server::ipc::gate_game::v1::ForwardPlayerMessageRequest request;
    if (!request.ParseFromArray(envelope.payload_bytes.data(), static_cast<int>(envelope.payload_bytes.size())))
    {
        return ipc::DispatchResult::Failure("failed to parse ForwardPlayerMessageRequest");
    }

    const auto session = mSessionService->Snapshot(request.player_id());

    some_server::ipc::gate_game::v1::ForwardPlayerMessageResponse response;
    response.set_request_id(request.request_id());
    response.set_player_id(request.player_id());
    response.set_message_id(request.message_id());
    response.set_server_sequence(request.client_sequence() + 1);
    if (!session.has_value())
    {
        response.set_result_code(some_server::ipc::gate_game::v1::RESULT_CODE_PLAYER_NOT_FOUND);
        response.set_error_message("player session is not tracked");
    }
    else if (session->state != PlayerSessionState::online)
    {
        response.set_result_code(some_server::ipc::gate_game::v1::RESULT_CODE_SESSION_EXPIRED);
        response.set_error_message("player session is not online");
    }
    else if (!mLeaseService->IsHeldLocally(request.player_id()))
    {
        response.set_result_code(some_server::ipc::gate_game::v1::RESULT_CODE_PLAYER_HELD_BY_OTHER_GAME);
        response.set_error_message("player lease is not held locally");
    }
    else
    {
        const auto it = mHandlers.find(request.message_id());
        if (it == mHandlers.end())
        {
            response.set_result_code(some_server::ipc::gate_game::v1::RESULT_CODE_INVALID_ARGUMENT);
            response.set_error_message("unsupported player message id");
        }
        else
        {
            std::string response_payload;
            const auto result = it->second(request.player_id(), request.payload_bytes(), response_payload);
            if (!result.ok)
            {
                response.set_result_code(some_server::ipc::gate_game::v1::RESULT_CODE_INVALID_ARGUMENT);
                response.set_error_message(result.message);
            }
            else
            {
                response.set_result_code(some_server::ipc::gate_game::v1::RESULT_CODE_OK);
                response.set_response_payload_bytes(std::move(response_payload));
            }
        }
    }

    const auto send = mIpcService->SendProcessPayload(envelope.header.source_process.process_id, response);
    return send.ok ? ipc::DispatchResult::Success() : ipc::DispatchResult::Failure(send.message);
}

ipc::Result GamePlayerMessageService::PushToPlayer(
    const std::uint64_t player_id,
    const std::uint32_t message_id,
    const std::string_view payload)
{
    if (mSessionService == nullptr || mLeaseService == nullptr || mIpcService == nullptr)
    {
        return ipc::Result::Failure("game player push dependencies are not registered");
    }

    const auto session = mSessionService->Snapshot(player_id);
    if (!session.has_value() || session->state != PlayerSessionState::online || session->gate_service_type == 0)
    {
        return ipc::Result::Failure("player is not online");
    }
    if (!mLeaseService->IsHeldLocally(player_id))
    {
        return ipc::Result::Failure("player lease is not held locally");
    }

    some_server::ipc::gate_game::v1::PushPlayerMessage push;
    push.set_player_id(player_id);
    push.set_gate_service_type(session->gate_service_type);
    push.set_gate_instance_id(session->gate_instance_id);
    push.set_gate_session_id(session->gate_session_id);
    push.set_message_id(message_id);
    push.set_payload_bytes(std::string{payload});
    push.set_push_sequence(session->activate_count);

    const auto send = mIpcService->SendProcessPayload(
        ipc::ProcessId{
            .service_type = session->gate_service_type,
            .instance_id = session->gate_instance_id},
        push);
    return send.ok ? ipc::Result::Success() : ipc::Result::Failure(send.message);
}

ipc::Result GamePlayerMessageService::PushProfileToPlayer(const std::uint64_t player_id)
{
    const auto payload = BuildProfilePushPayload(player_id);
    if (!payload.has_value())
    {
        return ipc::Result::Failure("failed to build player profile push payload");
    }
    return PushToPlayer(player_id, kPlayerProfilePushMessageId, *payload);
}

void GamePlayerMessageService::RegisterBuiltinHandlers()
{
    RegisterHandler<client::game::v1::PlayerEchoRequest, client::game::v1::PlayerEchoResponse>(
        kEchoMessageId,
        [this](
            const std::uint64_t player_id,
            const client::game::v1::PlayerEchoRequest& request,
            client::game::v1::PlayerEchoResponse& response) { return HandleEcho(player_id, request, response); });

    RegisterHandler<client::game::v1::RenamePlayerRequest, client::game::v1::RenamePlayerResponse>(
        kRenamePlayerMessageId,
        [this](
            const std::uint64_t player_id,
            const client::game::v1::RenamePlayerRequest& request,
            client::game::v1::RenamePlayerResponse& response) { return HandleRename(player_id, request, response); });
}

ipc::Result GamePlayerMessageService::HandleEcho(
    const std::uint64_t player_id,
    const client::game::v1::PlayerEchoRequest& request,
    client::game::v1::PlayerEchoResponse& response)
{
    return mRuntimeService->HandleEcho(player_id, request, response);
}

ipc::Result GamePlayerMessageService::HandleRename(
    const std::uint64_t player_id,
    const client::game::v1::RenamePlayerRequest& request,
    client::game::v1::RenamePlayerResponse& response)
{
    return mRuntimeService->HandleRename(player_id, request, response);
}

std::optional<std::string> GamePlayerMessageService::BuildProfilePushPayload(const std::uint64_t player_id) const
{
    if (mRuntimeService == nullptr)
    {
        return std::nullopt;
    }
    return mRuntimeService->BuildProfilePushPayload(player_id);
}
