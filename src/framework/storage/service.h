#pragma once

#include "../application/service_base.h"
#include "connectivity_probe.h"
#include "resolved_configuration.h"

#include <hiredis/hiredis.h>
#include <mariadb/mysql.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace some_server::storage
{
struct StorageSnapshot
{
    std::size_t dataset_count = 0;
    std::map<std::string, ConnectionProbeStatus> redis;
    std::map<std::string, ConnectionProbeStatus> maria;
};

struct StorageCommandResult
{
    bool ok = false;
    std::string message;
};

class StorageService final : public ServiceBase
{
public:
    explicit StorageService(const ResolvedStorageConfiguration& configuration);

    LifecycleTask Load() override;
    LifecycleTask Unload() override;
    StorageSnapshot Snapshot() const;
    StorageSnapshot ProbeNow();

    redisContext* GetRedis(std::string_view name) const;
    MYSQL* GetMaria(std::string_view name) const;
    redisContext* GetRedisForDataset(std::string_view dataset_name) const;
    MYSQL* GetMariaForDataset(std::string_view dataset_name) const;
    const ResolvedDatasetConfiguration* GetDataset(std::string_view dataset_name) const;

    std::string BuildRedisKey(std::string_view dataset_name, std::string_view suffix) const;
    std::string BuildMariaEntriesTableName(std::string_view dataset_name) const;
    StorageCommandResult EnsureMariaEntriesTable(std::string_view dataset_name);
    StorageCommandResult RedisSet(std::string_view dataset_name, std::string_view suffix, std::string_view value);
    StorageCommandResult RedisGet(std::string_view dataset_name, std::string_view suffix, std::string& value) const;
    StorageCommandResult RedisDelete(std::string_view dataset_name, std::string_view suffix);
    StorageCommandResult DatasetPut(std::string_view dataset_name, std::string_view entry_key, std::string_view value);
    StorageCommandResult DatasetGet(std::string_view dataset_name, std::string_view entry_key, std::string& value);
    StorageCommandResult DatasetDelete(std::string_view dataset_name, std::string_view entry_key);
    StorageCommandResult MariaExecute(std::string_view dataset_name, std::string_view sql, std::string& summary);

private:
    struct RedisConnectionState
    {
        const RedisConfiguration* configuration = nullptr;
        redisContext* context = nullptr;
        ConnectionProbeStatus status;
    };

    struct MariaConnectionState
    {
        const MariaConfiguration* configuration = nullptr;
        MYSQL* handle = nullptr;
        ConnectionProbeStatus status;
    };

    void RefreshConnections();
    void RefreshRedisConnection(const std::string& name, RedisConnectionState& state);
    void RefreshMariaConnection(const std::string& name, MariaConnectionState& state);
    void CloseConnections();
    StorageSnapshot ProbeAll() const;
    static StorageCommandResult Failure(std::string message);

    const ResolvedStorageConfiguration& mConfiguration;
    std::map<std::string, RedisConnectionState> mRedisConnections;
    std::map<std::string, MariaConnectionState> mMariaConnections;
};
}
