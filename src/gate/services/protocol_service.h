#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/result.h"

#include <common/v1/types.pb.h>
#include <game/v1/player.pb.h>
#include <login/v1/login.pb.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

struct GateProtocolFrame
{
    std::uint32_t message_id = 0;
    std::string payload;
};

struct GateDecodedLoginRequest
{
    client::login::v1::LoginRequest message;
};

struct GateDecodedPlayerMessageRequest
{
    client::game::v1::PlayerMessageRequest message;
};

struct GateProtocolEncodeResult
{
    bool ok = false;
    std::uint32_t message_id = 0;
    std::size_t encoded_size = 0;
    std::string payload;
    std::string message;
};

struct GateProtocolSnapshot
{
    std::uint32_t login_response_message_id = 2001;
    std::uint32_t heartbeat_response_message_id = 2002;
    std::uint32_t kick_notification_message_id = 2003;
    std::uint32_t player_message_request_id = 3001;
    std::uint32_t player_message_response_id = 3002;
    std::uint32_t player_push_message_id = 3003;
    std::size_t encoded_login_response_bytes = 0;
    std::size_t encoded_heartbeat_response_bytes = 0;
    std::size_t encoded_kick_notification_bytes = 0;
    std::size_t encoded_player_message_response_bytes = 0;
    std::size_t encoded_player_push_bytes = 0;
};

class GateProtocolService final : public ServiceBase
{
public:
    GateProtocolService()
        : ServiceBase("gate_protocol", 40)
    {
    }

    static constexpr std::uint32_t kLoginRequestMessageId = 1001;
    static constexpr std::uint32_t kHeartbeatRequestMessageId = 1002;
    static constexpr std::uint32_t kLoginResponseMessageId = 2001;
    static constexpr std::uint32_t kHeartbeatResponseMessageId = 2002;
    static constexpr std::uint32_t kKickNotificationMessageId = 2003;
    static constexpr std::uint32_t kPlayerMessageRequestMessageId = 3001;
    static constexpr std::uint32_t kPlayerMessageResponseMessageId = 3002;
    static constexpr std::uint32_t kPlayerPushMessageId = 3003;

    GateProtocolEncodeResult EncodeLoginResponse(
        std::uint64_t player_id,
        bool is_reconnect,
        client::common::v1::ErrorCode error_code = client::common::v1::ERROR_CODE_OK,
        std::string_view error_message = {});
    GateProtocolEncodeResult EncodeHeartbeatResponse(std::uint64_t server_time_ms);
    GateProtocolEncodeResult EncodeKickNotification(std::string_view reason);
    GateProtocolEncodeResult EncodePlayerMessageResponse(
        std::uint32_t message_id,
        std::string_view payload,
        client::common::v1::ErrorCode error_code = client::common::v1::ERROR_CODE_OK,
        std::string_view error_message = {});
    GateProtocolEncodeResult EncodePlayerPushMessage(std::uint32_t message_id, std::string_view payload);
    std::optional<GateDecodedLoginRequest> DecodeLoginRequest(const std::string& payload) const;
    std::optional<GateDecodedPlayerMessageRequest> DecodePlayerMessageRequest(const std::string& payload) const;
    bool IsHeartbeatRequest(std::uint32_t message_id) const { return message_id == kHeartbeatRequestMessageId; }
    bool IsPlayerMessageRequest(std::uint32_t message_id) const { return message_id == kPlayerMessageRequestMessageId; }
    GateProtocolSnapshot Snapshot() const;

private:
    mutable std::mutex mMutex;
    GateProtocolSnapshot mSnapshot;
};
