#include "player_message_service.h"

#include "connection_service.h"
#include "ipc_service.h"
#include "protocol_service.h"
#include "session_service.h"

#include <common.pb.h>
#include <ipc/gate_game/v1/player_message.pb.h>
#include <ipc/gate_game/v1/push.pb.h>

#include "../../framework/ipc/messaging/payload_registry.h"

ipc::Result GatePlayerMessageService::HandleClientPlayerMessage(
    const std::uint64_t connection_id,
    const pb::PlayerMessageRequest& client_request)
{
    if (mConnectionService == nullptr || mSessionService == nullptr || mIpcService == nullptr)
    {
        return ipc::Result::Failure("gate player message dependencies are not registered");
    }

    const auto session = mSessionService->FindByConnection(connection_id);
    if (!session.has_value() || session->state != GateSessionState::active || session->player_id == 0)
    {
        return ipc::Result::Failure("gate session is not active");
    }

    some_server::ipc::gate_game::v1::ForwardPlayerMessageRequest forward_request;
    forward_request.set_request_id(mNextRequestId++);
    forward_request.set_gate_service_type(session->game_service_type == 0 ? 20 : 20);
    forward_request.set_gate_instance_id(session->game_instance_id == 0 ? 1 : 1);
    forward_request.set_gate_session_id(session->gate_session_id);
    forward_request.set_player_id(session->player_id);
    forward_request.set_message_id(client_request.header().message_id());
    forward_request.set_payload_bytes(client_request.payload());
    forward_request.set_client_sequence(client_request.header().sequence());
    forward_request.set_timestamp_ms(client_request.header().timestamp_ms());

    const auto send = mIpcService->SendProcessPayload(
        ipc::ProcessId{
            .service_type = session->game_service_type,
            .instance_id = session->game_instance_id},
        forward_request);
    if (!send.ok)
    {
        return send;
    }

    std::scoped_lock lock(mMutex);
    mPendingMessages[forward_request.request_id()] = PendingMessage{
        .connection_id = connection_id,
        .gate_session_id = session->gate_session_id,
        .player_id = session->player_id,
        .client_message_id = client_request.header().message_id()};
    return ipc::Result::Success();
}

ipc::DispatchResult GatePlayerMessageService::HandleProcessEnvelope(const ipc::ReceiverAddress&, const ipc::Envelope& envelope)
{
    if (envelope.payload_type_url ==
        ipc::PayloadRegistry::TypeUrlFor(some_server::ipc::gate_game::v1::ForwardPlayerMessageResponse{}))
    {
        some_server::ipc::gate_game::v1::ForwardPlayerMessageResponse response;
        if (!response.ParseFromArray(envelope.payload_bytes.data(), static_cast<int>(envelope.payload_bytes.size())))
        {
            return ipc::DispatchResult::Failure("failed to parse ForwardPlayerMessageResponse");
        }

        PendingMessage pending;
        {
            std::scoped_lock lock(mMutex);
            const auto it = mPendingMessages.find(response.request_id());
            if (it == mPendingMessages.end())
            {
                return ipc::DispatchResult::Failure("missing pending player message");
            }
            pending = it->second;
            mPendingMessages.erase(it);
        }

        const auto encoded = mProtocolService->EncodePlayerMessageResponse(
            response.message_id(),
            response.response_payload_bytes(),
            response.result_code() == some_server::ipc::gate_game::v1::RESULT_CODE_OK
                ? pb::ERROR_CODE_OK
                : pb::ERROR_CODE_INTERNAL,
            response.error_message());
        if (!encoded.ok)
        {
            return ipc::DispatchResult::Failure(encoded.message);
        }

        const auto send = mConnectionService->Send(pending.connection_id, encoded.message_id, encoded.payload);
        return send.ok ? ipc::DispatchResult::Success() : ipc::DispatchResult::Failure(send.message);
    }

    if (envelope.payload_type_url ==
        ipc::PayloadRegistry::TypeUrlFor(some_server::ipc::gate_game::v1::PushPlayerMessage{}))
    {
        some_server::ipc::gate_game::v1::PushPlayerMessage push;
        if (!push.ParseFromArray(envelope.payload_bytes.data(), static_cast<int>(envelope.payload_bytes.size())))
        {
            return ipc::DispatchResult::Failure("failed to parse PushPlayerMessage");
        }

        const auto session = mSessionService->Snapshot(push.gate_session_id());
        if (!session.has_value() || session->connection_id == 0 || session->player_id != push.player_id())
        {
            return ipc::DispatchResult::Failure("gate session for push is not available");
        }

        const auto encoded = mProtocolService->EncodePlayerPushMessage(push.message_id(), push.payload_bytes());
        if (!encoded.ok)
        {
            return ipc::DispatchResult::Failure(encoded.message);
        }

        const auto send = mConnectionService->Send(session->connection_id, encoded.message_id, encoded.payload);
        return send.ok ? ipc::DispatchResult::Success() : ipc::DispatchResult::Failure(send.message);
    }

    return ipc::DispatchResult::Success();
}
