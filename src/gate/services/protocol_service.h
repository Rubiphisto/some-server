#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/result.h"
#include "../../common/protocol/protobuf_dispatcher.h"

#include <common.pb.h>
#include <login.pb.h>
#include <message_ids.pb.h>
#include <player.pb.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

struct GateProtocolFrame
{
    std::uint32_t message_id = 0;
    std::string payload;
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

    template <typename Message, typename HandlerFn>
    void RegisterClientHandler(const std::uint32_t message_id, HandlerFn&& handler)
    {
        mClientDispatcher.Register<Message>(message_id, std::forward<HandlerFn>(handler));
    }

    ipc::Result DispatchClientMessage(std::uint64_t connection_id, std::uint32_t message_id, const std::string& payload) const
    {
        return mClientDispatcher.Dispatch(message_id, payload, connection_id);
    }

    GateProtocolEncodeResult EncodeLoginResponse(
        std::uint64_t player_id,
        bool is_reconnect,
        pb::ErrorCode error_code = pb::ERROR_CODE_OK,
        std::string_view error_message = {});
    GateProtocolEncodeResult EncodeHeartbeatResponse(std::uint64_t server_time_ms);
    GateProtocolEncodeResult EncodeKickNotification(std::string_view reason);
    GateProtocolEncodeResult EncodePlayerMessageResponse(
        std::uint32_t message_id,
        std::string_view payload,
        pb::ErrorCode error_code = pb::ERROR_CODE_OK,
        std::string_view error_message = {});
    GateProtocolEncodeResult EncodePlayerPushMessage(std::uint32_t message_id, std::string_view payload);
    GateProtocolSnapshot Snapshot() const;

private:
    mutable std::mutex mMutex;
    GateProtocolSnapshot mSnapshot;
    common::protocol::ProtobufMessageDispatcher<std::uint64_t> mClientDispatcher;
};
