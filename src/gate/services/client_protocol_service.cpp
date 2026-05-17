#include "client_protocol_service.h"

#include <common.pb.h>
#include <login.pb.h>
#include <message_ids.pb.h>

#include <ctime>

GateClientProtocolService::GateClientProtocolService()
    : ServiceBase("gate_client_protocol", 40)
{
    RegisterBuiltinHandlers();
}

void GateClientProtocolService::RegisterBuiltinHandlers()
{
    RegisterClientHandler<pb::HeartbeatRequest>(
        pb::MESSAGE_ID_HEARTBEAT_REQUEST,
        [this](const std::uint64_t connection_id, const pb::HeartbeatRequest&) {
            return SendHeartbeatResponse(connection_id, static_cast<std::uint64_t>(std::time(nullptr)) * 1000);
        });
}

ipc::Result GateClientProtocolService::SendLoginResponse(
    const std::uint64_t connection_id,
    const std::uint64_t player_id,
    const bool is_reconnect,
    const pb::ErrorCode error_code,
    const std::string_view error_message)
{
    pb::LoginResponse response;
    response.mutable_header()->set_message_id(pb::MESSAGE_ID_LOGIN_RESPONSE);
    response.mutable_header()->set_sequence(1);
    response.mutable_header()->set_error_code(error_code);
    response.mutable_header()->set_error_message(std::string{error_message});
    response.set_player_id(player_id);
    response.set_is_reconnect(is_reconnect);
    return SendMessage(
        connection_id,
        pb::MESSAGE_ID_LOGIN_RESPONSE,
        response,
        &GateClientProtocolSnapshot::encoded_login_response_bytes);
}

ipc::Result GateClientProtocolService::SendHeartbeatResponse(
    const std::uint64_t connection_id,
    const std::uint64_t server_time_ms)
{
    pb::HeartbeatResponse response;
    response.mutable_header()->set_message_id(pb::MESSAGE_ID_HEARTBEAT_RESPONSE);
    response.mutable_header()->set_sequence(1);
    response.mutable_header()->set_error_code(pb::ERROR_CODE_OK);
    response.set_server_time_ms(server_time_ms);
    return SendMessage(
        connection_id,
        pb::MESSAGE_ID_HEARTBEAT_RESPONSE,
        response,
        &GateClientProtocolSnapshot::encoded_heartbeat_response_bytes);
}

ipc::Result GateClientProtocolService::SendKickNotification(
    const std::uint64_t connection_id,
    const std::string_view reason)
{
    pb::KickNotification notification;
    notification.set_reason(std::string{reason});
    return SendMessage(
        connection_id,
        pb::MESSAGE_ID_KICK_NOTIFICATION,
        notification,
        &GateClientProtocolSnapshot::encoded_kick_notification_bytes);
}

ipc::Result GateClientProtocolService::SendPlayerMessageResponse(
    const std::uint64_t connection_id,
    const std::uint32_t message_id,
    const std::string_view payload,
    const pb::ErrorCode error_code,
    const std::string_view error_message)
{
    pb::PlayerMessageResponse response;
    response.mutable_header()->set_message_id(message_id);
    response.mutable_header()->set_sequence(1);
    response.mutable_header()->set_error_code(error_code);
    response.mutable_header()->set_error_message(std::string{error_message});
    response.set_payload(std::string{payload});
    return SendMessage(
        connection_id,
        pb::MESSAGE_ID_PLAYER_MESSAGE_RESPONSE,
        response,
        &GateClientProtocolSnapshot::encoded_player_message_response_bytes);
}

ipc::Result GateClientProtocolService::SendPlayerPushMessage(
    const std::uint64_t connection_id,
    const std::uint32_t message_id,
    const std::string_view payload)
{
    pb::PlayerPushMessage push;
    push.set_message_id(message_id);
    push.set_payload(std::string{payload});
    return SendMessage(
        connection_id,
        pb::MESSAGE_ID_PLAYER_PUSH,
        push,
        &GateClientProtocolSnapshot::encoded_player_push_bytes);
}

GateClientProtocolSnapshot GateClientProtocolService::Snapshot() const
{
    std::scoped_lock lock(mMutex);
    return mSnapshot;
}
