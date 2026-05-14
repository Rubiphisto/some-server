#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/storage/service.h"

#include <atomic>
#include <optional>
#include <mutex>
#include <string>
#include <unordered_map>

struct PlayerDirectoryResolveResult
{
    std::uint64_t player_id = 0;
    bool created = false;
};

class PlayerDirectoryService final : public ServiceBase
{
public:
    PlayerDirectoryService(some_server::storage::StorageService* storage_service, std::string dataset_name)
        : ServiceBase("player_directory", 10)
        , mStorageService(storage_service)
        , mDatasetName(std::move(dataset_name))
    {
    }

    PlayerDirectoryResolveResult ResolveOrCreate(
        std::string_view platform,
        std::string_view account_id,
        std::uint32_t area_id);
    std::optional<std::uint64_t> Lookup(
        std::string_view platform,
        std::string_view account_id,
        std::uint32_t area_id);
    std::optional<std::uint64_t> NextPlayerIdCounter();

private:
    static std::string MakeKey(std::string_view platform, std::string_view account_id, std::uint32_t area_id);
    std::string BuildDirectoryEntrySuffix(std::string_view key) const;
    std::string BuildNextPlayerIdSuffix() const;

    some_server::storage::StorageService* mStorageService = nullptr;
    std::string mDatasetName;
    std::mutex mMutex;
    std::unordered_map<std::string, std::uint64_t> mEntries;
    std::atomic<std::uint64_t> mNextPlayerId = 100000;
};
