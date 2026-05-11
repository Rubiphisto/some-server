#pragma once

#include "configuration.h"
#include "../application/configuration.h"

#include <map>
#include <string>

namespace some_server::storage
{
struct ResolvedDatasetConfiguration
{
    std::string name;
    std::string redis_name;
    std::string maria_name;
    const RedisConfiguration* redis = nullptr;
    const MariaConfiguration* maria = nullptr;
    std::string redis_prefix;
    std::string table_prefix;
    std::string full_redis_prefix;
    DatasetTimingConfiguration timing;
};

class ResolvedStorageConfiguration
{
public:
    bool Empty() const { return mDatasets.empty(); }
    std::size_t Size() const { return mDatasets.size(); }

    const std::map<std::string, ResolvedDatasetConfiguration>& Datasets() const { return mDatasets; }

    const ResolvedDatasetConfiguration* FindDataset(const std::string& name) const
    {
        const auto it = mDatasets.find(name);
        return it == mDatasets.end() ? nullptr : &it->second;
    }

    void Clear() { mDatasets.clear(); }
    void AddDataset(ResolvedDatasetConfiguration configuration)
    {
        mDatasets.emplace(configuration.name, std::move(configuration));
    }

private:
    std::map<std::string, ResolvedDatasetConfiguration> mDatasets;
};

bool ResolveStorageConfiguration(const CommonConfiguration& common,
                                 const StorageConfiguration& storage,
                                 ResolvedStorageConfiguration& resolved,
                                 std::string& error);
}
