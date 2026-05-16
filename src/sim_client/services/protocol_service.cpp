#include "protocol_service.h"

#include <common.pb.h>
#include <google/protobuf/message.h>
#include <login.pb.h>
#include <player.pb.h>

namespace
{
constexpr std::uint32_t kPlayerMessageRequestFrameId = 3001;
constexpr std::uint32_t kPlayerMessageResponseFrameId = 3002;
constexpr std::uint32_t kPlayerPushFrameId = 3003;
constexpr std::uint32_t kEchoMessageId = 3101;
constexpr std::uint32_t kRenamePlayerMessageId = 3102;
constexpr std::uint32_t kPlayerProfilePushMessageId = 5101;

std::optional<SimClientProtocolFrame> BuildTypedPlayerFrame(
    const std::uint32_t message_id,
    const google::protobuf::Message& payload_message)
{
    std::string inner_payload;
    if (!payload_message.SerializeToString(&inner_payload))
    {
        return std::nullopt;
    }

    client::game::v1::PlayerMessageRequest request;
    request.mutable_header()->set_message_id(message_id);
    request.mutable_header()->set_sequence(3);
    request.mutable_header()->set_timestamp_ms(3);
    request.set_payload(std::move(inner_payload));

    SimClientProtocolFrame frame;
    frame.message_id = kPlayerMessageRequestFrameId;
    if (!request.SerializeToString(&frame.payload))
    {
        return std::nullopt;
    }
    return frame;
}
}

ProtocolService::ProtocolService(SimClientConfiguration configuration)
    : ServiceBase("sim_client_protocol", 20), mConfiguration(std::move(configuration))
{
    mSnapshot.configured_platform = mConfiguration.default_platform;
    mSnapshot.configured_account_id = mConfiguration.default_account_id;
    mSnapshot.configured_channel = mConfiguration.default_channel;
    mSnapshot.configured_client_version = mConfiguration.default_client_version;
}

ipc::Result ProtocolService::SetAccountId(const std::string_view account_id)
{
    if (account_id.empty())
    {
        return ipc::Result::Failure("account_id must not be empty");
    }

    std::scoped_lock lock(mMutex);
    mConfiguration.default_account_id = std::string{account_id};
    mSnapshot.configured_account_id = mConfiguration.default_account_id;
    return ipc::Result::Success();
}

ipc::Result ProtocolService::ResetRuntimeState()
{
    std::scoped_lock lock(mMutex);
    mSnapshot.encoded_login_bytes = 0;
    mSnapshot.encoded_heartbeat_bytes = 0;
    mSnapshot.received_login_response_bytes = 0;
    mSnapshot.last_login_player_id = 0;
    mSnapshot.last_login_ok = false;
    mSnapshot.last_login_error_code = 0;
    mSnapshot.last_login_error_message.clear();
    mSnapshot.last_kick_reason.clear();
    mSnapshot.encoded_player_message_bytes = 0;
    mSnapshot.last_player_response_message_id = 0;
    mSnapshot.last_player_response_error_code = 0;
    mSnapshot.last_player_response_error_message.clear();
    mSnapshot.last_player_response_payload.clear();
    mSnapshot.last_echo_text.clear();
    mSnapshot.last_rename_display_name.clear();
    mSnapshot.last_push_message_id = 0;
    mSnapshot.last_push_payload.clear();
    mSnapshot.last_profile_push_display_name.clear();
    return ipc::Result::Success();
}

ProtocolEncodeResult ProtocolService::EncodeDefaultLogin()
{
    client::login::v1::LoginRequest request;
    request.mutable_header()->set_message_id(1001);
    request.mutable_header()->set_sequence(1);
    request.mutable_header()->set_timestamp_ms(1);
    request.set_platform(mConfiguration.default_platform);
    request.set_account_id(mConfiguration.default_account_id);
    request.set_credential("sim-token");
    request.set_client_version(mConfiguration.default_client_version);
    request.set_channel(mConfiguration.default_channel);

    std::string payload;
    if (!request.SerializeToString(&payload))
    {
        return ProtocolEncodeResult{.ok = false, .message = "failed to serialize login request"};
    }

    std::scoped_lock lock(mMutex);
    mSnapshot.encoded_login_bytes = payload.size();
    return ProtocolEncodeResult{
        .ok = true,
        .message_id = 1001,
        .encoded_size = payload.size(),
        .message = "OK"};
}

ProtocolEncodeResult ProtocolService::EncodeHeartbeat()
{
    client::login::v1::HeartbeatRequest request;
    request.mutable_header()->set_message_id(1002);
    request.mutable_header()->set_sequence(2);
    request.mutable_header()->set_timestamp_ms(2);

    std::string payload;
    if (!request.SerializeToString(&payload))
    {
        return ProtocolEncodeResult{.ok = false, .message = "failed to serialize heartbeat request"};
    }

    std::scoped_lock lock(mMutex);
    mSnapshot.encoded_heartbeat_bytes = payload.size();
    return ProtocolEncodeResult{
        .ok = true,
        .message_id = 1002,
        .encoded_size = payload.size(),
        .message = "OK"};
}

ProtocolEncodeResult ProtocolService::EncodePlayerMessage(const std::string_view payload)
{
    return EncodeEchoRequest(payload);
}

ProtocolEncodeResult ProtocolService::EncodeEchoRequest(const std::string_view text)
{
    client::game::v1::PlayerEchoRequest request;
    request.set_text(std::string{text});

    const auto frame = BuildTypedPlayerFrame(kEchoMessageId, request);
    if (!frame.has_value())
    {
        return ProtocolEncodeResult{.ok = false, .message = "failed to serialize echo request"};
    }

    std::scoped_lock lock(mMutex);
    mSnapshot.encoded_player_message_bytes = frame->payload.size();
    return ProtocolEncodeResult{
        .ok = true,
        .message_id = frame->message_id,
        .encoded_size = frame->payload.size(),
        .message = "OK"};
}

ProtocolEncodeResult ProtocolService::EncodeRenameRequest(const std::string_view display_name)
{
    client::game::v1::RenamePlayerRequest request;
    request.set_display_name(std::string{display_name});

    const auto frame = BuildTypedPlayerFrame(kRenamePlayerMessageId, request);
    if (!frame.has_value())
    {
        return ProtocolEncodeResult{.ok = false, .message = "failed to serialize rename player request"};
    }

    std::scoped_lock lock(mMutex);
    mSnapshot.encoded_player_message_bytes = frame->payload.size();
    return ProtocolEncodeResult{
        .ok = true,
        .message_id = frame->message_id,
        .encoded_size = frame->payload.size(),
        .message = "OK"};
}

SimClientProtocolSnapshot ProtocolService::Snapshot() const
{
    std::scoped_lock lock(mMutex);
    return mSnapshot;
}

std::optional<SimClientProtocolFrame> ProtocolService::BuildDefaultLoginFrame()
{
    const auto encoded = EncodeDefaultLogin();
    if (!encoded.ok)
    {
        return std::nullopt;
    }

    client::login::v1::LoginRequest request;
    request.mutable_header()->set_message_id(1001);
    request.mutable_header()->set_sequence(1);
    request.mutable_header()->set_timestamp_ms(1);
    request.set_platform(mConfiguration.default_platform);
    request.set_account_id(mConfiguration.default_account_id);
    request.set_credential("sim-token");
    request.set_client_version(mConfiguration.default_client_version);
    request.set_channel(mConfiguration.default_channel);

    SimClientProtocolFrame frame;
    frame.message_id = 1001;
    if (!request.SerializeToString(&frame.payload))
    {
        return std::nullopt;
    }
    return frame;
}

std::optional<SimClientProtocolFrame> ProtocolService::BuildHeartbeatFrame()
{
    const auto encoded = EncodeHeartbeat();
    if (!encoded.ok)
    {
        return std::nullopt;
    }

    client::login::v1::HeartbeatRequest request;
    request.mutable_header()->set_message_id(1002);
    request.mutable_header()->set_sequence(2);
    request.mutable_header()->set_timestamp_ms(2);

    SimClientProtocolFrame frame;
    frame.message_id = 1002;
    if (!request.SerializeToString(&frame.payload))
    {
        return std::nullopt;
    }
    return frame;
}

std::optional<SimClientProtocolFrame> ProtocolService::BuildPlayerMessageFrame(const std::string_view payload)
{
    return BuildEchoFrame(payload);
}

std::optional<SimClientProtocolFrame> ProtocolService::BuildEchoFrame(const std::string_view text)
{
    const auto encoded = EncodeEchoRequest(text);
    if (!encoded.ok)
    {
        return std::nullopt;
    }

    client::game::v1::PlayerEchoRequest request;
    request.set_text(std::string{text});
    return BuildTypedPlayerFrame(kEchoMessageId, request);
}

std::optional<SimClientProtocolFrame> ProtocolService::BuildRenameFrame(const std::string_view display_name)
{
    const auto encoded = EncodeRenameRequest(display_name);
    if (!encoded.ok)
    {
        return std::nullopt;
    }

    client::game::v1::RenamePlayerRequest request;
    request.set_display_name(std::string{display_name});
    return BuildTypedPlayerFrame(kRenamePlayerMessageId, request);
}

ipc::Result ProtocolService::HandleFrame(const std::uint32_t message_id, const std::string& payload)
{
    if (message_id == 2001)
    {
        client::login::v1::LoginResponse response;
        if (!response.ParseFromString(payload))
        {
            return ipc::Result::Failure("failed to parse LoginResponse");
        }
        std::scoped_lock lock(mMutex);
        mSnapshot.received_login_response_bytes = payload.size();
        mSnapshot.last_login_player_id = response.player_id();
        mSnapshot.last_login_ok = response.header().error_code() == client::common::v1::ERROR_CODE_OK;
        mSnapshot.last_login_error_code = static_cast<std::uint32_t>(response.header().error_code());
        mSnapshot.last_login_error_message = response.header().error_message();
        return ipc::Result::Success();
    }

    if (message_id == 2002)
    {
        client::login::v1::HeartbeatResponse response;
        if (!response.ParseFromString(payload))
        {
            return ipc::Result::Failure("failed to parse HeartbeatResponse");
        }
        return ipc::Result::Success();
    }

    if (message_id == 2003)
    {
        client::login::v1::KickNotification notification;
        if (!notification.ParseFromString(payload))
        {
            return ipc::Result::Failure("failed to parse KickNotification");
        }
        std::scoped_lock lock(mMutex);
        mSnapshot.last_kick_reason = notification.reason();
        return ipc::Result::Success();
    }

    if (message_id == kPlayerMessageResponseFrameId)
    {
        client::game::v1::PlayerMessageResponse response;
        if (!response.ParseFromString(payload))
        {
            return ipc::Result::Failure("failed to parse PlayerMessageResponse");
        }

        std::scoped_lock lock(mMutex);
        mSnapshot.last_player_response_message_id = response.header().message_id();
        mSnapshot.last_player_response_error_code = static_cast<std::uint32_t>(response.header().error_code());
        mSnapshot.last_player_response_error_message = response.header().error_message();
        mSnapshot.last_player_response_payload = response.payload();

        if (response.header().message_id() == kEchoMessageId)
        {
            client::game::v1::PlayerEchoResponse echo;
            if (!echo.ParseFromString(response.payload()))
            {
                return ipc::Result::Failure("failed to parse PlayerEchoResponse");
            }
            mSnapshot.last_echo_text = echo.text();
            mSnapshot.last_player_response_payload = echo.text();
        }
        else if (response.header().message_id() == kRenamePlayerMessageId)
        {
            client::game::v1::RenamePlayerResponse rename;
            if (!rename.ParseFromString(response.payload()))
            {
                return ipc::Result::Failure("failed to parse RenamePlayerResponse");
            }
            mSnapshot.last_rename_display_name = rename.display_name();
            mSnapshot.last_player_response_payload = rename.display_name();
        }
        return ipc::Result::Success();
    }

    if (message_id == kPlayerPushFrameId)
    {
        client::game::v1::PlayerPushMessage push;
        if (!push.ParseFromString(payload))
        {
            return ipc::Result::Failure("failed to parse PlayerPushMessage");
        }

        std::scoped_lock lock(mMutex);
        mSnapshot.last_push_message_id = push.message_id();
        mSnapshot.last_push_payload = push.payload();

        if (push.message_id() == kPlayerProfilePushMessageId)
        {
            client::game::v1::PlayerProfilePush profile;
            if (!profile.ParseFromString(push.payload()))
            {
                return ipc::Result::Failure("failed to parse PlayerProfilePush");
            }
            mSnapshot.last_profile_push_display_name = profile.display_name();
            mSnapshot.last_push_payload = profile.display_name();
        }
        return ipc::Result::Success();
    }

    return ipc::Result::Failure("unsupported message_id");
}
