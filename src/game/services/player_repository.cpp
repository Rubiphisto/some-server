#include "player_repository.h"

#include <chrono>
#include <string>
#include <vector>

ipc::Result PlayerRepository::LoadPlayer(const std::uint64_t player_id, const bool allow_create_without_maria)
{
    if (mStorageService == nullptr)
    {
        return ipc::Result::Failure("storage service is not registered");
    }
    if (mStorageService->GetDataset(mDatasetName) == nullptr)
    {
        return ipc::Result::Failure("player dataset is not configured");
    }

    std::scoped_lock lock(mMutex);
    auto& record = mPlayers[player_id];
    std::string value;
    const auto result = mStorageService->DatasetGet(mDatasetName, std::to_string(player_id), value);
    if (result.ok)
    {
        if (!record.data.ParseFromString(value))
        {
            return ipc::Result::Failure("stored player data is corrupted");
        }
    }
    else if (result.message == "not found")
    {
        InitializeDefaultPlayerData(record, player_id);
        record.dirty = true;
        record.pending_initial_persist = true;
        record.created_without_maria = false;
        record.last_dirty_ms = NowMs();
    }
    else if (allow_create_without_maria && result.message == "maria connection is unavailable")
    {
        InitializeDefaultPlayerData(record, player_id);
        record.dirty = true;
        record.pending_initial_persist = true;
        record.created_without_maria = true;
        record.last_dirty_ms = NowMs();
    }
    else
    {
        return ipc::Result::Failure(result.message);
    }

    record.loaded = true;
    record.last_load_ms = NowMs();
    ++record.load_count;
    return ipc::Result::Success();
}

ipc::Result PlayerRepository::FlushPlayer(const std::uint64_t player_id)
{
    std::scoped_lock lock(mMutex);
    const auto it = mPlayers.find(player_id);
    if (it == mPlayers.end() || !it->second.loaded)
    {
        return ipc::Result::Failure("player is not loaded in repository");
    }

    std::string payload;
    if (!it->second.data.SerializeToString(&payload))
    {
        return ipc::Result::Failure("failed to serialize player data");
    }
    const auto store_result = mStorageService->DatasetPut(mDatasetName, std::to_string(player_id), payload);
    if (!store_result.ok)
    {
        return ipc::Result::Failure(store_result.message);
    }

    it->second.dirty = false;
    it->second.pending_initial_persist = false;
    it->second.created_without_maria = false;
    it->second.last_dirty_ms = 0;
    it->second.last_flush_ms = NowMs();
    ++it->second.flush_count;
    return ipc::Result::Success();
}

ipc::Result PlayerRepository::ReleasePlayer(const std::uint64_t player_id)
{
    std::scoped_lock lock(mMutex);
    const auto it = mPlayers.find(player_id);
    if (it == mPlayers.end())
    {
        return ipc::Result::Failure("player is not tracked by repository");
    }
    mPlayers.erase(it);
    return ipc::Result::Success();
}

ipc::Result PlayerRepository::TouchLogin(const std::uint64_t player_id)
{
    std::scoped_lock lock(mMutex);
    const auto it = mPlayers.find(player_id);
    if (it == mPlayers.end() || !it->second.loaded)
    {
        return ipc::Result::Failure("player is not loaded in repository");
    }

    it->second.data.mutable_base()->set_last_login_at_ms(NowMs());
    it->second.data.mutable_core()->set_login_count(it->second.data.core().login_count() + 1);
    it->second.dirty = true;
    it->second.last_dirty_ms = NowMs();
    return ipc::Result::Success();
}

ipc::Result PlayerRepository::SetDisplayName(const std::uint64_t player_id, const std::string_view display_name)
{
    std::scoped_lock lock(mMutex);
    const auto it = mPlayers.find(player_id);
    if (it == mPlayers.end() || !it->second.loaded)
    {
        return ipc::Result::Failure("player is not loaded in repository");
    }

    it->second.data.mutable_base()->set_display_name(std::string{display_name});
    it->second.dirty = true;
    it->second.last_dirty_ms = NowMs();
    return ipc::Result::Success();
}

std::optional<PlayerRepositoryRecord> PlayerRepository::Snapshot(const std::uint64_t player_id) const
{
    std::scoped_lock lock(mMutex);
    const auto it = mPlayers.find(player_id);
    if (it == mPlayers.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::uint64_t> PlayerRepository::LoadedPlayerIds() const
{
    std::scoped_lock lock(mMutex);
    std::vector<std::uint64_t> players;
    players.reserve(mPlayers.size());
    for (const auto& [player_id, record] : mPlayers)
    {
        if (record.loaded)
        {
            players.push_back(player_id);
        }
    }
    return players;
}

std::uint64_t PlayerRepository::NowMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

void PlayerRepository::InitializeDefaultPlayerData(PlayerRepositoryRecord& record, const std::uint64_t player_id)
{
    record.loaded = false;
    record.dirty = false;
    record.pending_initial_persist = false;
    record.created_without_maria = false;
    record.load_count = 0;
    record.flush_count = 0;
    record.last_load_ms = 0;
    record.last_dirty_ms = 0;
    record.last_flush_ms = 0;
    record.data.Clear();
    record.data.mutable_base()->set_player_id(player_id);
    record.data.mutable_base()->set_display_name("player_" + std::to_string(player_id));
    record.data.mutable_base()->set_created_at_ms(NowMs());
    record.data.mutable_core()->set_level(1);
    record.data.mutable_core()->set_experience(0);
    record.data.mutable_core()->set_login_count(0);
}
