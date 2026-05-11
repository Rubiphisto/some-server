#include "service.h"

#include <hiredis/hiredis.h>

#include <sstream>

namespace some_server::storage
{
StorageService::StorageService(const ResolvedStorageConfiguration& configuration)
    : ServiceBase("storage", 0), mConfiguration(configuration)
{
    for (const auto& [dataset_name, dataset] : mConfiguration.Datasets())
    {
        (void)dataset_name;
        auto& redis = mRedisConnections[dataset.redis_name];
        redis.configuration = dataset.redis;
        redis.status.name = dataset.redis_name;

        auto& maria = mMariaConnections[dataset.maria_name];
        maria.configuration = dataset.maria;
        maria.status.name = dataset.maria_name;
    }
}

LifecycleTask StorageService::Load()
{
    RefreshConnections();
    return LifecycleTask::Completed();
}

LifecycleTask StorageService::Unload()
{
    CloseConnections();
    return LifecycleTask::Completed();
}

StorageSnapshot StorageService::Snapshot() const
{
    return ProbeAll();
}

StorageSnapshot StorageService::ProbeNow()
{
    RefreshConnections();
    return ProbeAll();
}

redisContext* StorageService::GetRedis(std::string_view name) const
{
    const auto it = mRedisConnections.find(std::string{name});
    return it == mRedisConnections.end() ? nullptr : it->second.context;
}

MYSQL* StorageService::GetMaria(std::string_view name) const
{
    const auto it = mMariaConnections.find(std::string{name});
    return it == mMariaConnections.end() ? nullptr : it->second.handle;
}

redisContext* StorageService::GetRedisForDataset(std::string_view dataset_name) const
{
    const auto* dataset = mConfiguration.FindDataset(std::string{dataset_name});
    return dataset == nullptr ? nullptr : GetRedis(dataset->redis_name);
}

MYSQL* StorageService::GetMariaForDataset(std::string_view dataset_name) const
{
    const auto* dataset = mConfiguration.FindDataset(std::string{dataset_name});
    return dataset == nullptr ? nullptr : GetMaria(dataset->maria_name);
}

const ResolvedDatasetConfiguration* StorageService::GetDataset(std::string_view dataset_name) const
{
    return mConfiguration.FindDataset(std::string{dataset_name});
}

std::string StorageService::BuildRedisKey(std::string_view dataset_name, std::string_view suffix) const
{
    const auto* dataset = GetDataset(dataset_name);
    if (dataset == nullptr)
    {
        return {};
    }
    std::string key = dataset->full_redis_prefix;
    key += suffix;
    return key;
}

StorageCommandResult StorageService::RedisSet(std::string_view dataset_name,
                                              std::string_view suffix,
                                              std::string_view value)
{
    redisContext* redis = GetRedisForDataset(dataset_name);
    if (redis == nullptr)
    {
        return Failure("redis connection is unavailable");
    }
    const std::string key = BuildRedisKey(dataset_name, suffix);
    if (key.empty())
    {
        return Failure("dataset is unknown");
    }

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(redis, "SET %b %b", key.data(), key.size(), value.data(), value.size()));
    if (reply == nullptr)
    {
        return Failure(redis->err != 0 && redis->errstr[0] != '\0' ? redis->errstr : "redis SET failed");
    }
    const bool ok = reply->type == REDIS_REPLY_STATUS && reply->str != nullptr && std::string_view(reply->str) == "OK";
    const std::string message = ok ? "OK" : (reply->str != nullptr ? reply->str : "redis SET failed");
    freeReplyObject(reply);
    return {ok, message};
}

StorageCommandResult StorageService::RedisGet(std::string_view dataset_name,
                                              std::string_view suffix,
                                              std::string& value) const
{
    redisContext* redis = GetRedisForDataset(dataset_name);
    if (redis == nullptr)
    {
        return Failure("redis connection is unavailable");
    }
    const std::string key = BuildRedisKey(dataset_name, suffix);
    if (key.empty())
    {
        return Failure("dataset is unknown");
    }

    redisReply* reply = static_cast<redisReply*>(redisCommand(redis, "GET %b", key.data(), key.size()));
    if (reply == nullptr)
    {
        return Failure(redis->err != 0 && redis->errstr[0] != '\0' ? redis->errstr : "redis GET failed");
    }
    if (reply->type == REDIS_REPLY_NIL)
    {
        freeReplyObject(reply);
        return {false, "nil"};
    }
    if (reply->type != REDIS_REPLY_STRING)
    {
        const std::string message = reply->str != nullptr ? reply->str : "redis GET returned unexpected reply";
        freeReplyObject(reply);
        return {false, message};
    }
    value.assign(reply->str, static_cast<std::size_t>(reply->len));
    freeReplyObject(reply);
    return {true, "OK"};
}

StorageCommandResult StorageService::RedisDelete(std::string_view dataset_name, std::string_view suffix)
{
    redisContext* redis = GetRedisForDataset(dataset_name);
    if (redis == nullptr)
    {
        return Failure("redis connection is unavailable");
    }
    const std::string key = BuildRedisKey(dataset_name, suffix);
    if (key.empty())
    {
        return Failure("dataset is unknown");
    }

    redisReply* reply = static_cast<redisReply*>(redisCommand(redis, "DEL %b", key.data(), key.size()));
    if (reply == nullptr)
    {
        return Failure(redis->err != 0 && redis->errstr[0] != '\0' ? redis->errstr : "redis DEL failed");
    }
    if (reply->type != REDIS_REPLY_INTEGER)
    {
        const std::string message = reply->str != nullptr ? reply->str : "redis DEL returned unexpected reply";
        freeReplyObject(reply);
        return {false, message};
    }
    const auto deleted = static_cast<long long>(reply->integer);
    freeReplyObject(reply);
    return {true, "deleted=" + std::to_string(deleted)};
}

StorageCommandResult StorageService::MariaExecute(std::string_view dataset_name,
                                                  std::string_view sql,
                                                  std::string& summary)
{
    MYSQL* maria = GetMariaForDataset(dataset_name);
    if (maria == nullptr)
    {
        return Failure("maria connection is unavailable");
    }
    if (sql.empty())
    {
        return Failure("sql must not be empty");
    }

    if (mysql_query(maria, std::string{sql}.c_str()) != 0)
    {
        return Failure(mysql_error(maria));
    }

    MYSQL_RES* result = mysql_store_result(maria);
    if (result == nullptr)
    {
        if (mysql_field_count(maria) == 0)
        {
            summary = "affected_rows=" + std::to_string(mysql_affected_rows(maria));
            return {true, "OK"};
        }
        return Failure(mysql_error(maria));
    }

    const unsigned int field_count = mysql_num_fields(result);
    std::size_t row_count = 0;
    std::ostringstream stream;
    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(result)) != nullptr && row_count < 10)
    {
        unsigned long* lengths = mysql_fetch_lengths(result);
        if (row_count != 0)
        {
            stream << " | ";
        }
        stream << "row" << row_count << "=";
        for (unsigned int i = 0; i < field_count; ++i)
        {
            if (i != 0)
            {
                stream << ",";
            }
            if (row[i] == nullptr)
            {
                stream << "NULL";
            }
            else
            {
                stream.write(row[i], static_cast<std::streamsize>(lengths[i]));
            }
        }
        ++row_count;
    }
    mysql_free_result(result);
    summary = "rows=" + std::to_string(row_count);
    if (!stream.str().empty())
    {
        summary += " " + stream.str();
    }
    return {true, "OK"};
}

void StorageService::RefreshConnections()
{
    for (auto& [name, state] : mRedisConnections)
    {
        RefreshRedisConnection(name, state);
    }
    for (auto& [name, state] : mMariaConnections)
    {
        RefreshMariaConnection(name, state);
    }
}

void StorageService::RefreshRedisConnection(const std::string& name, RedisConnectionState& state)
{
    state.status.name = name;
    if (state.configuration == nullptr)
    {
        state.status.reachable = false;
        state.status.error = "redis configuration is missing";
        return;
    }

    if (state.context != nullptr)
    {
        std::string error;
        if (PingRedis(*state.context, error))
        {
            state.status.reachable = true;
            state.status.error.clear();
            return;
        }
        state.status.error = std::move(error);
        CloseRedis(state.context);
        state.context = nullptr;
    }

    const RedisConnectResult result = ConnectRedis(*state.configuration);
    state.status.target = result.target;
    state.status.error = result.error;
    state.context = result.context;
    if (state.context == nullptr)
    {
        state.status.reachable = false;
        return;
    }

    std::string ping_error;
    state.status.reachable = PingRedis(*state.context, ping_error);
    if (!state.status.reachable)
    {
        state.status.error = std::move(ping_error);
        CloseRedis(state.context);
        state.context = nullptr;
        return;
    }
    state.status.error.clear();
}

void StorageService::RefreshMariaConnection(const std::string& name, MariaConnectionState& state)
{
    state.status.name = name;
    if (state.configuration == nullptr)
    {
        state.status.reachable = false;
        state.status.error = "maria configuration is missing";
        return;
    }

    if (state.handle != nullptr)
    {
        std::string error;
        if (PingMaria(*state.handle, error))
        {
            state.status.reachable = true;
            state.status.error.clear();
            return;
        }
        state.status.error = std::move(error);
        CloseMaria(state.handle);
        state.handle = nullptr;
    }

    const MariaConnectResult result = ConnectMaria(*state.configuration);
    state.status.target = result.target;
    state.status.error = result.error;
    state.handle = result.handle;
    if (state.handle == nullptr)
    {
        state.status.reachable = false;
        return;
    }

    std::string ping_error;
    state.status.reachable = PingMaria(*state.handle, ping_error);
    if (!state.status.reachable)
    {
        state.status.error = std::move(ping_error);
        CloseMaria(state.handle);
        state.handle = nullptr;
        return;
    }
    state.status.error.clear();
}

void StorageService::CloseConnections()
{
    for (auto& [name, state] : mRedisConnections)
    {
        (void)name;
        CloseRedis(state.context);
        state.context = nullptr;
        state.status.reachable = false;
    }
    for (auto& [name, state] : mMariaConnections)
    {
        (void)name;
        CloseMaria(state.handle);
        state.handle = nullptr;
        state.status.reachable = false;
    }
}

StorageSnapshot StorageService::ProbeAll() const
{
    StorageSnapshot snapshot;
    snapshot.dataset_count = mConfiguration.Size();

    for (const auto& [name, state] : mRedisConnections)
    {
        snapshot.redis.emplace(name, state.status);
    }
    for (const auto& [name, state] : mMariaConnections)
    {
        snapshot.maria.emplace(name, state.status);
    }

    return snapshot;
}

StorageCommandResult StorageService::Failure(std::string message)
{
    return {false, std::move(message)};
}
}
