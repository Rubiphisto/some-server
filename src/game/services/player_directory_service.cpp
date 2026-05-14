#include "player_directory_service.h"

#include <sstream>

PlayerDirectoryResolveResult PlayerDirectoryService::ResolveOrCreate(
    std::string_view platform,
    std::string_view account_id,
    const std::uint32_t area_id)
{
    std::scoped_lock lock(mMutex);
    const auto key = MakeKey(platform, account_id, area_id);
    if (const auto it = mEntries.find(key); it != mEntries.end())
    {
        return PlayerDirectoryResolveResult{
            .player_id = it->second,
            .created = false};
    }

    if (mStorageService != nullptr)
    {
        std::string value;
        const auto get = mStorageService->RedisGet(mDatasetName, BuildDirectoryEntrySuffix(key), value);
        if (get.ok)
        {
            const auto player_id = static_cast<std::uint64_t>(std::stoull(value));
            mEntries.emplace(key, player_id);
            return PlayerDirectoryResolveResult{
                .player_id = player_id,
                .created = false};
        }

        while (true)
        {
            std::string current_counter;
            const auto current_counter_result =
                mStorageService->RedisGet(mDatasetName, BuildNextPlayerIdSuffix(), current_counter);
            if (!current_counter_result.ok)
            {
                (void)mStorageService->RedisSetIfAbsent(mDatasetName, BuildNextPlayerIdSuffix(), "99999");
            }
            else
            {
                try
                {
                    if (std::stoll(current_counter) < 99999)
                    {
                        (void)mStorageService->RedisSet(mDatasetName, BuildNextPlayerIdSuffix(), "99999");
                    }
                }
                catch (const std::exception&)
                {
                    (void)mStorageService->RedisSet(mDatasetName, BuildNextPlayerIdSuffix(), "99999");
                }
            }
            std::int64_t next_player_id = 0;
            const auto increment = mStorageService->RedisIncrement(mDatasetName, BuildNextPlayerIdSuffix(), 1, next_player_id);
            if (!increment.ok)
            {
                break;
            }

            const auto player_id = static_cast<std::uint64_t>(next_player_id);
            const auto create = mStorageService->RedisSetIfAbsent(
                mDatasetName,
                BuildDirectoryEntrySuffix(key),
                std::to_string(player_id));
            if (create.ok)
            {
                mEntries.emplace(key, player_id);
                return PlayerDirectoryResolveResult{
                    .player_id = player_id,
                    .created = true};
            }
            if (create.message != "exists")
            {
                break;
            }

            value.clear();
            const auto retry_get = mStorageService->RedisGet(mDatasetName, BuildDirectoryEntrySuffix(key), value);
            if (retry_get.ok)
            {
                const auto player_id = static_cast<std::uint64_t>(std::stoull(value));
                mEntries.emplace(key, player_id);
                return PlayerDirectoryResolveResult{
                    .player_id = player_id,
                    .created = false};
            }
        }
    }

    const std::uint64_t player_id = mNextPlayerId.fetch_add(1);
    mEntries.emplace(key, player_id);
    return PlayerDirectoryResolveResult{
        .player_id = player_id,
        .created = true};
}

std::optional<std::uint64_t> PlayerDirectoryService::Lookup(
    std::string_view platform,
    std::string_view account_id,
    const std::uint32_t area_id)
{
    std::scoped_lock lock(mMutex);
    const auto key = MakeKey(platform, account_id, area_id);
    if (const auto it = mEntries.find(key); it != mEntries.end())
    {
        return it->second;
    }
    if (mStorageService == nullptr)
    {
        return std::nullopt;
    }

    std::string value;
    const auto get = mStorageService->RedisGet(mDatasetName, BuildDirectoryEntrySuffix(key), value);
    if (!get.ok)
    {
        return std::nullopt;
    }

    const auto player_id = static_cast<std::uint64_t>(std::stoull(value));
    mEntries.emplace(key, player_id);
    return player_id;
}

std::optional<std::uint64_t> PlayerDirectoryService::NextPlayerIdCounter()
{
    if (mStorageService == nullptr)
    {
        return std::nullopt;
    }

    std::string value;
    const auto get = mStorageService->RedisGet(mDatasetName, BuildNextPlayerIdSuffix(), value);
    if (!get.ok)
    {
        return std::nullopt;
    }

    try
    {
        return static_cast<std::uint64_t>(std::stoull(value));
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

std::string PlayerDirectoryService::MakeKey(
    std::string_view platform,
    std::string_view account_id,
    const std::uint32_t area_id)
{
    std::ostringstream stream;
    stream << platform << ':' << account_id << ':' << area_id;
    return stream.str();
}

std::string PlayerDirectoryService::BuildDirectoryEntrySuffix(std::string_view key) const
{
    return "directory:entry:" + std::string{key};
}

std::string PlayerDirectoryService::BuildNextPlayerIdSuffix() const
{
    return "directory:next_player_id";
}
