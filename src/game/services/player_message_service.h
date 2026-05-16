#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/envelope.h"
#include "../../framework/ipc/base/result.h"

#include <player.pb.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

class GameIpcClientService;
class PlayerLeaseService;
class PlayerRepository;
class PlayerRuntimeService;
class PlayerSessionService;

class GamePlayerMessageService final : public ServiceBase
{
public:
    static constexpr std::uint32_t kEchoMessageId = 3101;
    static constexpr std::uint32_t kRenamePlayerMessageId = 3102;
    static constexpr std::uint32_t kPlayerProfilePushMessageId = 5101;

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
    using Handler = std::function<ipc::Result(std::uint64_t, const std::string&, std::string&)>;

    template <typename Request, typename Response, typename HandlerFn>
    void RegisterHandler(std::uint32_t message_id, HandlerFn&& handler)
    {
        mHandlers[message_id] =
            [callback = std::forward<HandlerFn>(handler)](
                std::uint64_t player_id,
                const std::string& payload,
                std::string& response_payload) -> ipc::Result {
            Request request;
            if (!request.ParseFromString(payload))
            {
                return ipc::Result::Failure("failed to parse request payload");
            }

            Response response;
            const auto result = callback(player_id, request, response);
            if (!result.ok)
            {
                return result;
            }
            if (!response.SerializeToString(&response_payload))
            {
                return ipc::Result::Failure("failed to serialize response payload");
            }
            return ipc::Result::Success();
        };
    }

    void RegisterBuiltinHandlers();
    ipc::Result HandleEcho(
        std::uint64_t player_id,
        const client::game::v1::PlayerEchoRequest& request,
        client::game::v1::PlayerEchoResponse& response);
    ipc::Result HandleRename(
        std::uint64_t player_id,
        const client::game::v1::RenamePlayerRequest& request,
        client::game::v1::RenamePlayerResponse& response);
    std::optional<std::string> BuildProfilePushPayload(std::uint64_t player_id) const;

    PlayerSessionService* mSessionService = nullptr;
    PlayerLeaseService* mLeaseService = nullptr;
    PlayerRepository* mRepository = nullptr;
    PlayerRuntimeService* mRuntimeService = nullptr;
    GameIpcClientService* mIpcService = nullptr;
    std::unordered_map<std::uint32_t, Handler> mHandlers;
};
