#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/result.h"
#include "../../framework/storage/service.h"

#include <player_data.pb.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct PlayerRepositoryRecord
{
    bool loaded = false;
    bool dirty = false;
    bool pending_initial_persist = false;
    bool created_without_maria = false;
    std::uint64_t load_count = 0;
    std::uint64_t flush_count = 0;
    std::uint64_t last_load_ms = 0;
    std::uint64_t last_dirty_ms = 0;
    std::uint64_t last_flush_ms = 0;
    some_server::player::v1::PlayerData data;
};

class PlayerRepository final : public ServiceBase
{
public:
    PlayerRepository(some_server::storage::StorageService* storage_service, std::string dataset_name)
        : ServiceBase("player_repository", 20)
        , mStorageService(storage_service)
        , mDatasetName(std::move(dataset_name))
    {
    }

    ipc::Result LoadPlayer(std::uint64_t player_id, bool allow_create_without_maria = false);
    ipc::Result FlushPlayer(std::uint64_t player_id);
    ipc::Result ReleasePlayer(std::uint64_t player_id);
    ipc::Result TouchLogin(std::uint64_t player_id);
    ipc::Result SetDisplayName(std::uint64_t player_id, std::string_view display_name);
    std::optional<PlayerRepositoryRecord> Snapshot(std::uint64_t player_id) const;
    std::vector<std::uint64_t> LoadedPlayerIds() const;

private:
    static std::uint64_t NowMs();
    static void InitializeDefaultPlayerData(PlayerRepositoryRecord& record, std::uint64_t player_id);

    some_server::storage::StorageService* mStorageService = nullptr;
    std::string mDatasetName;
    mutable std::mutex mMutex;
    std::unordered_map<std::uint64_t, PlayerRepositoryRecord> mPlayers;
};
