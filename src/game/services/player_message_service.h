#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/envelope.h"
#include "../../framework/ipc/base/result.h"
#include "../../common/protocol/protobuf_dispatcher.h"

#include <message_ids.pb.h>
#include <player.pb.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

class GameIpcClientService;
class PlayerLeaseService;
class PlayerRepository;
class PlayerRuntimeService;
class PlayerSessionService;

class GamePlayerMessageService final : public ServiceBase
{
public:
    GamePlayerMessageService(
        PlayerSessionService* session_service,
        PlayerLeaseService* lease_service,
        PlayerRepository* repository,
        PlayerRuntimeService* runtime_service,
        GameIpcClientService* ipc_service)
        : ServiceBase("game_player_message", 50)
        , mSessionService(session_service)
        , mLeaseService(lease_service)
        , mRepository(repository)
        , mRuntimeService(runtime_service)
        , mIpcService(ipc_service)
    {
        RegisterBuiltinHandlers();
    }

    ipc::DispatchResult HandleProcessEnvelope(const ipc::ReceiverAddress& target, const ipc::Envelope& envelope);
    ipc::Result PushToPlayer(std::uint64_t player_id, std::uint32_t message_id, std::string_view payload);
    ipc::Result PushProfileToPlayer(std::uint64_t player_id);

private:
    template <typename Request, typename Response, typename HandlerFn>
    void RegisterHandler(std::uint32_t message_id, HandlerFn&& handler)
    {
        mDispatcher.Register<Request, Response>(message_id, std::forward<HandlerFn>(handler));
    }

    void RegisterBuiltinHandlers();
    ipc::Result HandleEcho(
        std::uint64_t player_id,
        const pb::PlayerEchoRequest& request,
        pb::PlayerEchoResponse& response);
    ipc::Result HandleRename(
        std::uint64_t player_id,
        const pb::RenamePlayerRequest& request,
        pb::RenamePlayerResponse& response);
    std::optional<std::string> BuildProfilePushPayload(std::uint64_t player_id) const;

    PlayerSessionService* mSessionService = nullptr;
    PlayerLeaseService* mLeaseService = nullptr;
    PlayerRepository* mRepository = nullptr;
    PlayerRuntimeService* mRuntimeService = nullptr;
    GameIpcClientService* mIpcService = nullptr;
    common::protocol::ProtobufRequestResponseDispatcher<std::uint64_t> mDispatcher;
};
