#pragma once

#include "../application.h"
#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/result.h"

#include <message_ids.pb.h>

#include <cstdint>
#include <mutex>
#include <optional>

struct SimClientProtocolFrame
{
    std::uint32_t message_id = 0;
    std::string payload;
};

struct ProtocolEncodeResult
{
    bool ok = false;
    std::uint32_t message_id = 0;
    std::size_t encoded_size = 0;
    std::string message;
};

struct SimClientProtocolSnapshot
{
    std::size_t encoded_login_bytes = 0;
    std::size_t encoded_heartbeat_bytes = 0;
    std::size_t received_login_response_bytes = 0;
    std::string configured_platform;
    std::string configured_account_id;
    std::string configured_channel;
    std::uint32_t configured_client_version = 0;
    std::uint64_t last_login_player_id = 0;
    bool last_login_ok = false;
    std::uint32_t last_login_error_code = 0;
    std::string last_login_error_message;
    std::string last_kick_reason;
    std::size_t encoded_player_message_bytes = 0;
    std::uint32_t last_player_response_message_id = 0;
    std::uint32_t last_player_response_error_code = 0;
    std::string last_player_response_error_message;
    std::string last_player_response_payload;
    std::string last_echo_text;
    std::string last_rename_display_name;
    std::uint32_t last_push_message_id = 0;
    std::string last_push_payload;
    std::string last_profile_push_display_name;
};

class ProtocolService final : public ServiceBase
{
public:
    explicit ProtocolService(SimClientConfiguration configuration);

    ipc::Result SetAccountId(std::string_view account_id);
    ipc::Result ResetRuntimeState();
    ProtocolEncodeResult EncodeDefaultLogin();
    ProtocolEncodeResult EncodeHeartbeat();
    ProtocolEncodeResult EncodePlayerMessage(std::string_view payload);
    ProtocolEncodeResult EncodeEchoRequest(std::string_view text);
    ProtocolEncodeResult EncodeRenameRequest(std::string_view display_name);
    std::optional<SimClientProtocolFrame> BuildDefaultLoginFrame();
    std::optional<SimClientProtocolFrame> BuildHeartbeatFrame();
    std::optional<SimClientProtocolFrame> BuildPlayerMessageFrame(std::string_view payload);
    std::optional<SimClientProtocolFrame> BuildEchoFrame(std::string_view text);
    std::optional<SimClientProtocolFrame> BuildRenameFrame(std::string_view display_name);
    ipc::Result HandleFrame(std::uint32_t message_id, const std::string& payload);
    SimClientProtocolSnapshot Snapshot() const;

private:
    SimClientConfiguration mConfiguration;
    mutable std::mutex mMutex;
    SimClientProtocolSnapshot mSnapshot;
};
