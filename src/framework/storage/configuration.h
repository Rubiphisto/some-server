#pragma once

#include <glaze/glaze.hpp>

#include <cstdint>
#include <map>
#include <string>

namespace some_server::storage
{
struct DatasetTimingConfiguration
{
    std::uint32_t alive_time_seconds = 0;
    std::uint32_t landing_time_seconds = 0;
    std::uint32_t landing_min_time_seconds = 0;
};

struct DatasetConfiguration
{
    std::string redis = "default";
    std::string maria = "default";
    std::string redis_prefix;
    std::string table_prefix;
    DatasetTimingConfiguration timing;
};

struct StorageConfiguration
{
    DatasetTimingConfiguration defaults = {86400, 1800, 60};
    std::map<std::string, DatasetConfiguration> datasets;
};
}

template <>
struct glz::meta<some_server::storage::DatasetTimingConfiguration>
{
    using T = some_server::storage::DatasetTimingConfiguration;
    static constexpr auto value = glz::object(
        "alive_time_seconds",
        &T::alive_time_seconds,
        "landing_time_seconds",
        &T::landing_time_seconds,
        "landing_min_time_seconds",
        &T::landing_min_time_seconds);
};

template <>
struct glz::meta<some_server::storage::DatasetConfiguration>
{
    using T = some_server::storage::DatasetConfiguration;
    static constexpr auto value = glz::object(
        "redis",
        &T::redis,
        "maria",
        &T::maria,
        "redis_prefix",
        &T::redis_prefix,
        "table_prefix",
        &T::table_prefix,
        "timing",
        &T::timing);
};

template <>
struct glz::meta<some_server::storage::StorageConfiguration>
{
    using T = some_server::storage::StorageConfiguration;
    static constexpr auto value = glz::object("defaults", &T::defaults, "datasets", &T::datasets);
};
