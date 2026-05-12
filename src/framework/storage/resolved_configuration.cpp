#include "resolved_configuration.h"

namespace some_server::storage
{
namespace
{
bool IsSafeTablePrefix(const std::string_view text)
{
    for (const unsigned char ch : text)
    {
        if (!std::isalnum(ch) && ch != '_')
        {
            return false;
        }
    }
    return !text.empty();
}

std::uint32_t ResolveTimingValue(const std::uint32_t dataset_value, const std::uint32_t default_value)
{
    return dataset_value != 0 ? dataset_value : default_value;
}
}

bool ResolveStorageConfiguration(const CommonConfiguration& common,
                                 const StorageConfiguration& storage,
                                 ResolvedStorageConfiguration& resolved,
                                 std::string& error)
{
    resolved.Clear();

    for (const auto& [dataset_name, dataset] : storage.datasets)
    {
        if (dataset.redis.empty())
        {
            error = "storage.datasets." + dataset_name + ".redis must not be empty";
            resolved.Clear();
            return false;
        }
        if (dataset.maria.empty())
        {
            error = "storage.datasets." + dataset_name + ".maria must not be empty";
            resolved.Clear();
            return false;
        }
        if (dataset.redis_prefix.empty())
        {
            error = "storage.datasets." + dataset_name + ".redis_prefix must not be empty";
            resolved.Clear();
            return false;
        }
        if (dataset.table_prefix.empty())
        {
            error = "storage.datasets." + dataset_name + ".table_prefix must not be empty";
            resolved.Clear();
            return false;
        }
        if (!IsSafeTablePrefix(dataset.table_prefix))
        {
            error = "storage.datasets." + dataset_name + ".table_prefix must contain only [A-Za-z0-9_]";
            resolved.Clear();
            return false;
        }

        const auto redis_it = common.redis.find(dataset.redis);
        if (redis_it == common.redis.end())
        {
            error = "storage.datasets." + dataset_name + ".redis references unknown profile '" + dataset.redis + "'";
            resolved.Clear();
            return false;
        }

        const auto maria_it = common.maria.find(dataset.maria);
        if (maria_it == common.maria.end())
        {
            error = "storage.datasets." + dataset_name + ".maria references unknown profile '" + dataset.maria + "'";
            resolved.Clear();
            return false;
        }

        ResolvedDatasetConfiguration item;
        item.name = dataset_name;
        item.redis_name = dataset.redis;
        item.maria_name = dataset.maria;
        item.redis = &redis_it->second;
        item.maria = &maria_it->second;
        item.redis_prefix = dataset.redis_prefix;
        item.table_prefix = dataset.table_prefix;
        item.full_redis_prefix = redis_it->second.key_prefix + dataset.redis_prefix;
        item.timing.alive_time_seconds =
            ResolveTimingValue(dataset.timing.alive_time_seconds, storage.defaults.alive_time_seconds);
        item.timing.landing_time_seconds =
            ResolveTimingValue(dataset.timing.landing_time_seconds, storage.defaults.landing_time_seconds);
        item.timing.landing_min_time_seconds =
            ResolveTimingValue(dataset.timing.landing_min_time_seconds, storage.defaults.landing_min_time_seconds);

        if (item.timing.alive_time_seconds == 0)
        {
            error = "storage.datasets." + dataset_name + " resolved alive_time_seconds must be greater than 0";
            resolved.Clear();
            return false;
        }
        if (item.timing.landing_time_seconds == 0)
        {
            error = "storage.datasets." + dataset_name + " resolved landing_time_seconds must be greater than 0";
            resolved.Clear();
            return false;
        }
        if (item.timing.landing_min_time_seconds == 0)
        {
            error = "storage.datasets." + dataset_name + " resolved landing_min_time_seconds must be greater than 0";
            resolved.Clear();
            return false;
        }

        resolved.AddDataset(std::move(item));
    }

    return true;
}
}
