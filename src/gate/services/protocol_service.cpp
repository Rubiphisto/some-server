#include "protocol_service.h"

#include <common.pb.h>
#include <login.pb.h>

GateProtocolEncodeResult GateProtocolService::EncodeLoginResponse(
    const std::uint64_t player_id,
    const bool is_reconnect,
    const client::common::v1::ErrorCode error_code,
    const std::string_view error_message)
{
    client::login::v1::LoginResponse response;
    response.mutable_header()->set_message_id(kLoginResponseMessageId);
    response.mutable_header()->set_sequence(1);
    response.mutable_header()->set_error_code(error_code);
    response.mutable_header()->set_error_message(std::string{error_message});
    response.set_player_id(player_id);
    response.set_is_reconnect(is_reconnect);

    std::string payload;
    if (!response.SerializeToString(&payload))
    {
        return GateProtocolEncodeResult{.ok = false, .message = "failed to serialize login response"};
    }

    std::scoped_lock lock(mMutex);
    mSnapshot.encoded_login_response_bytes = payload.size();
    return GateProtocolEncodeResult{
        .ok = true,
        .message_id = kLoginResponseMessageId,
        .encoded_size = payload.size(),
        .payload = std::move(payload),
        .message = "OK"};
}

GateProtocolEncodeResult GateProtocolService::EncodeHeartbeatResponse(const std::uint64_t server_time_ms)
{
    client::login::v1::HeartbeatResponse response;
    response.mutable_header()->set_message_id(kHeartbeatResponseMessageId);
    response.mutable_header()->set_sequence(1);
    response.mutable_header()->set_error_code(client::common::v1::ERROR_CODE_OK);
    response.set_server_time_ms(server_time_ms);

    std::string payload;
    if (!response.SerializeToString(&payload))
    {
        return GateProtocolEncodeResult{.ok = false, .message = "failed to serialize heartbeat response"};
    }

    std::scoped_lock lock(mMutex);
    mSnapshot.encoded_heartbeat_response_bytes = payload.size();
    return GateProtocolEncodeResult{
        .ok = true,
        .message_id = kHeartbeatResponseMessageId,
        .encoded_size = payload.size(),
        .payload = std::move(payload),
        .message = "OK"};
}

GateProtocolEncodeResult GateProtocolService::EncodeKickNotification(const std::string_view reason)
{
    client::login::v1::KickNotification notification;
    notification.set_reason(std::string{reason});

    std::string payload;
    if (!notification.SerializeToString(&payload))
    {
        return GateProtocolEncodeResult{.ok = false, .message = "failed to serialize kick notification"};
    }

    std::scoped_lock lock(mMutex);
    mSnapshot.encoded_kick_notification_bytes = payload.size();
    return GateProtocolEncodeResult{
        .ok = true,
        .message_id = kKickNotificationMessageId,
        .encoded_size = payload.size(),
        .payload = std::move(payload),
        .message = "OK"};
}

GateProtocolEncodeResult GateProtocolService::EncodePlayerMessageResponse(
    const std::uint32_t message_id,
    const std::string_view payload,
    const client::common::v1::ErrorCode error_code,
    const std::string_view error_message)
{
    client::game::v1::PlayerMessageResponse response;
    response.mutable_header()->set_message_id(message_id);
    response.mutable_header()->set_sequence(1);
    response.mutable_header()->set_error_code(error_code);
    response.mutable_header()->set_error_message(std::string{error_message});
    response.set_payload(std::string{payload});

    std::string bytes;
    if (!response.SerializeToString(&bytes))
    {
        return GateProtocolEncodeResult{.ok = false, .message = "failed to serialize player message response"};
    }

    std::scoped_lock lock(mMutex);
    mSnapshot.encoded_player_message_response_bytes = bytes.size();
    return GateProtocolEncodeResult{
        .ok = true,
        .message_id = kPlayerMessageResponseMessageId,
        .encoded_size = bytes.size(),
        .payload = std::move(bytes),
        .message = "OK"};
}

GateProtocolEncodeResult GateProtocolService::EncodePlayerPushMessage(
    const std::uint32_t message_id,
    const std::string_view payload)
{
    client::game::v1::PlayerPushMessage push;
    push.set_message_id(message_id);
    push.set_payload(std::string{payload});

    std::string bytes;
    if (!push.SerializeToString(&bytes))
    {
        return GateProtocolEncodeResult{.ok = false, .message = "failed to serialize player push message"};
    }

    std::scoped_lock lock(mMutex);
    mSnapshot.encoded_player_push_bytes = bytes.size();
    return GateProtocolEncodeResult{
        .ok = true,
        .message_id = kPlayerPushMessageId,
        .encoded_size = bytes.size(),
        .payload = std::move(bytes),
        .message = "OK"};
}

std::optional<GateDecodedLoginRequest> GateProtocolService::DecodeLoginRequest(const std::string& payload) const
{
    GateDecodedLoginRequest decoded;
    if (!decoded.message.ParseFromString(payload))
    {
        return std::nullopt;
    }
    return decoded;
}

std::optional<GateDecodedPlayerMessageRequest> GateProtocolService::DecodePlayerMessageRequest(const std::string& payload) const
{
    GateDecodedPlayerMessageRequest decoded;
    if (!decoded.message.ParseFromString(payload))
    {
        return std::nullopt;
    }
    return decoded;
}

GateProtocolSnapshot GateProtocolService::Snapshot() const
{
    std::scoped_lock lock(mMutex);
    return mSnapshot;
}
