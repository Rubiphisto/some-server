#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/result.h"
#include "player_repository.h"

#include <player.pb.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

class PlayerInstance final
{
public:
    PlayerInstance(std::uint64_t player_id, PlayerRepository* repository)
        : mPlayerId(player_id)
        , mRepository(repository)
    {
    }

    std::uint64_t PlayerId() const { return mPlayerId; }
    std::uint64_t CommandCount() const { return mCommandCount; }

    ipc::Result HandleEcho(
        const client::game::v1::PlayerEchoRequest& request,
        client::game::v1::PlayerEchoResponse& response);
    ipc::Result HandleRename(
        const client::game::v1::RenamePlayerRequest& request,
        client::game::v1::RenamePlayerResponse& response);
    std::optional<std::string> BuildProfilePushPayload() const;

private:
    std::uint64_t mPlayerId = 0;
    PlayerRepository* mRepository = nullptr;
    std::uint64_t mCommandCount = 0;
};

struct PlayerRuntimeSnapshot
{
    bool present = false;
    std::uint64_t player_id = 0;
    std::uint64_t command_count = 0;
};

class PlayerRuntimeService final : public ServiceBase
{
public:
    explicit PlayerRuntimeService(PlayerRepository* repository)
        : ServiceBase("player_runtime", 25)
        , mRepository(repository)
    {
    }

    ipc::Result EnsurePlayer(std::uint64_t player_id);
    ipc::Result RemovePlayer(std::uint64_t player_id);
    ipc::Result HandleEcho(
        std::uint64_t player_id,
        const client::game::v1::PlayerEchoRequest& request,
        client::game::v1::PlayerEchoResponse& response);
    ipc::Result HandleRename(
        std::uint64_t player_id,
        const client::game::v1::RenamePlayerRequest& request,
        client::game::v1::RenamePlayerResponse& response);
    std::optional<std::string> BuildProfilePushPayload(std::uint64_t player_id) const;
    PlayerRuntimeSnapshot Snapshot(std::uint64_t player_id) const;

private:
    PlayerRepository* mRepository = nullptr;
    mutable std::mutex mMutex;
    std::unordered_map<std::uint64_t, std::unique_ptr<PlayerInstance>> mPlayers;
};
