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
#include <functional>
#include <mutex>
#include <string>
#include <utility>

struct GateClientProtocolSnapshot
{
    std::size_t encoded_login_response_bytes = 0;
    std::size_t encoded_heartbeat_response_bytes = 0;
    std::size_t encoded_kick_notification_bytes = 0;
    std::size_t encoded_player_message_response_bytes = 0;
    std::size_t encoded_player_push_bytes = 0;
};

class GateClientProtocolService final : public ServiceBase
{
public:
    using SendHandler = std::function<ipc::Result(std::uint64_t, std::uint32_t, const std::string&)>;

    GateClientProtocolService();

    template <typename Message, typename HandlerFn>
    void RegisterClientHandler(const std::uint32_t message_id, HandlerFn&& handler)
    {
        mClientDispatcher.Register<Message>(message_id, std::forward<HandlerFn>(handler));
    }

    ipc::Result DispatchClientMessage(std::uint64_t connection_id, std::uint32_t message_id, const std::string& payload) const
    {
        return mClientDispatcher.Dispatch(message_id, payload, connection_id);
    }

    void SetSendHandler(SendHandler handler) { mSendHandler = std::move(handler); }

    ipc::Result SendLoginResponse(
        std::uint64_t connection_id,
        std::uint64_t player_id,
        bool is_reconnect,
        pb::ErrorCode error_code = pb::ERROR_CODE_OK,
        std::string_view error_message = {});
    ipc::Result SendHeartbeatResponse(std::uint64_t connection_id, std::uint64_t server_time_ms);
    ipc::Result SendKickNotification(std::uint64_t connection_id, std::string_view reason);
    ipc::Result SendPlayerMessageResponse(
        std::uint64_t connection_id,
        std::uint32_t message_id,
        std::string_view payload,
        pb::ErrorCode error_code = pb::ERROR_CODE_OK,
        std::string_view error_message = {});
    ipc::Result SendPlayerPushMessage(std::uint64_t connection_id, std::uint32_t message_id, std::string_view payload);
    GateClientProtocolSnapshot Snapshot() const;

private:
    void RegisterBuiltinHandlers();

    template <typename Message>
    ipc::Result SendMessage(
        const std::uint64_t connection_id,
        const std::uint32_t frame_message_id,
        const Message& message,
        std::size_t GateClientProtocolSnapshot::* counter)
    {
        if (!mSendHandler)
        {
            return ipc::Result::Failure("gate client protocol send handler is not registered");
        }

        std::string payload;
        if (!message.SerializeToString(&payload))
        {
            return ipc::Result::Failure("failed to serialize protobuf payload");
        }

        {
            std::scoped_lock lock(mMutex);
            mSnapshot.*counter = payload.size();
        }
        return mSendHandler(connection_id, frame_message_id, payload);
    }

    mutable std::mutex mMutex;
    GateClientProtocolSnapshot mSnapshot;
    common::protocol::ProtobufMessageDispatcher<std::uint64_t> mClientDispatcher;
    SendHandler mSendHandler;
};
