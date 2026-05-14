#include "service.h"

#include <hiredis/hiredis.h>

#include <cctype>
#include <sstream>

namespace some_server::storage
{
namespace
{
bool IsSafeSqlIdentifier(std::string_view text)
{
    if (text.empty())
    {
        return false;
    }
    for (const unsigned char ch : text)
    {
        if (!std::isalnum(ch) && ch != '_')
        {
            return false;
        }
    }
    return true;
}

std::string EscapeMariaLiteral(MYSQL& handle, std::string_view text)
{
    std::string escaped;
    escaped.resize(text.size() * 2 + 1);
    const unsigned long size = mysql_real_escape_string(
        &handle,
        escaped.data(),
        text.data(),
        static_cast<unsigned long>(text.size()));
    escaped.resize(size);
    return escaped;
}
}

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
    if (it == mMariaConnections.end())
    {
        return nullptr;
    }
    if (mDisabledMariaTargets.contains(std::string{name}))
    {
        return nullptr;
    }
    return it->second.handle;
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

std::string StorageService::BuildMariaEntriesTableName(std::string_view dataset_name) const
{
    const auto* dataset = GetDataset(dataset_name);
    if (dataset == nullptr || !IsSafeSqlIdentifier(dataset->table_prefix))
    {
        return {};
    }
    return dataset->table_prefix + "entries";
}

StorageCommandResult StorageService::EnsureMariaEntriesTable(std::string_view dataset_name)
{
    MYSQL* maria = GetMariaForDataset(dataset_name);
    if (maria == nullptr)
    {
        return Failure("maria connection is unavailable");
    }
    const std::string table = BuildMariaEntriesTableName(dataset_name);
    if (table.empty())
    {
        return Failure("dataset table_prefix is invalid");
    }

    const std::string sql =
        "CREATE TABLE IF NOT EXISTS `" + table +
        "` ("
        "`entry_key` VARCHAR(255) NOT NULL,"
        "`entry_value` LONGBLOB NOT NULL,"
        "`updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "PRIMARY KEY (`entry_key`)"
        ")";
    if (mysql_query(maria, sql.c_str()) != 0)
    {
        return Failure(mysql_error(maria));
    }

    return {true, "OK"};
}

StorageCommandResult StorageService::MigrateMariaEntriesTableToBinary(std::string_view dataset_name)
{
    MYSQL* maria = GetMariaForDataset(dataset_name);
    if (maria == nullptr)
    {
        return Failure("maria connection is unavailable");
    }
    const auto ensure = EnsureMariaEntriesTable(dataset_name);
    if (!ensure.ok)
    {
        return ensure;
    }

    const std::string table = BuildMariaEntriesTableName(dataset_name);
    if (table.empty())
    {
        return Failure("dataset table_prefix is invalid");
    }

    const std::string alter_sql =
        "ALTER TABLE `" + table + "` MODIFY COLUMN `entry_value` LONGBLOB NOT NULL";
    if (mysql_query(maria, alter_sql.c_str()) != 0)
    {
        return Failure(mysql_error(maria));
    }
    return {true, "OK"};
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

StorageCommandResult StorageService::RedisSetIfAbsent(std::string_view dataset_name,
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
        redisCommand(redis, "SET %b %b NX", key.data(), key.size(), value.data(), value.size()));
    if (reply == nullptr)
    {
        return Failure(redis->err != 0 && redis->errstr[0] != '\0' ? redis->errstr : "redis SET NX failed");
    }
    if (reply->type == REDIS_REPLY_NIL)
    {
        freeReplyObject(reply);
        return {false, "exists"};
    }
    const bool ok = reply->type == REDIS_REPLY_STATUS && reply->str != nullptr && std::string_view(reply->str) == "OK";
    const std::string message = ok ? "OK" : (reply->str != nullptr ? reply->str : "redis SET NX failed");
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

StorageCommandResult StorageService::RedisIncrement(std::string_view dataset_name,
                                                    std::string_view suffix,
                                                    const std::int64_t delta,
                                                    std::int64_t& value)
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
        redisCommand(redis, "INCRBY %b %lld", key.data(), key.size(), static_cast<long long>(delta)));
    if (reply == nullptr)
    {
        return Failure(redis->err != 0 && redis->errstr[0] != '\0' ? redis->errstr : "redis INCRBY failed");
    }
    if (reply->type != REDIS_REPLY_INTEGER)
    {
        const std::string message = reply->str != nullptr ? reply->str : "redis INCRBY returned unexpected reply";
        freeReplyObject(reply);
        return {false, message};
    }
    value = static_cast<std::int64_t>(reply->integer);
    freeReplyObject(reply);
    return {true, "OK"};
}

StorageCommandResult StorageService::DatasetPut(std::string_view dataset_name,
                                                std::string_view entry_key,
                                                std::string_view value)
{
    MYSQL* maria = GetMariaForDataset(dataset_name);
    if (maria == nullptr)
    {
        return Failure("maria connection is unavailable");
    }
    if (entry_key.empty())
    {
        return Failure("entry_key must not be empty");
    }

    const auto ensure = EnsureMariaEntriesTable(dataset_name);
    if (!ensure.ok)
    {
        return ensure;
    }

    const std::string table = BuildMariaEntriesTableName(dataset_name);
    const std::string escaped_key = EscapeMariaLiteral(*maria, entry_key);
    const std::string escaped_value = EscapeMariaLiteral(*maria, value);
    const std::string sql =
        "INSERT INTO `" + table + "` (`entry_key`, `entry_value`) VALUES ('" + escaped_key + "', '" + escaped_value +
        "') ON DUPLICATE KEY UPDATE `entry_value`=VALUES(`entry_value`)";
    if (mysql_query(maria, sql.c_str()) != 0)
    {
        return Failure(mysql_error(maria));
    }

    const auto redis = RedisSet(dataset_name, std::string{"entries:"} + std::string{entry_key}, value);
    if (!redis.ok)
    {
        return {true, "maria=OK redis=" + redis.message};
    }
    return {true, "OK"};
}

StorageCommandResult StorageService::DatasetGet(std::string_view dataset_name,
                                                std::string_view entry_key,
                                                std::string& value)
{
    if (entry_key.empty())
    {
        return Failure("entry_key must not be empty");
    }

    const auto redis_result = RedisGet(dataset_name, std::string{"entries:"} + std::string{entry_key}, value);
    if (redis_result.ok)
    {
        return {true, "redis"};
    }
    if (redis_result.message != "nil")
    {
        return redis_result;
    }

    MYSQL* maria = GetMariaForDataset(dataset_name);
    if (maria == nullptr)
    {
        return Failure("maria connection is unavailable");
    }
    const auto ensure = EnsureMariaEntriesTable(dataset_name);
    if (!ensure.ok)
    {
        return ensure;
    }
    const std::string table = BuildMariaEntriesTableName(dataset_name);
    const std::string escaped_key = EscapeMariaLiteral(*maria, entry_key);
    const std::string sql =
        "SELECT `entry_value` FROM `" + table + "` WHERE `entry_key`='" + escaped_key + "' LIMIT 1";
    if (mysql_query(maria, sql.c_str()) != 0)
    {
        return Failure(mysql_error(maria));
    }
    MYSQL_RES* result = mysql_store_result(maria);
    if (result == nullptr)
    {
        return Failure(mysql_error(maria));
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr || row[0] == nullptr)
    {
        mysql_free_result(result);
        return Failure("not found");
    }
    unsigned long* lengths = mysql_fetch_lengths(result);
    value.assign(row[0], static_cast<std::size_t>(lengths[0]));
    mysql_free_result(result);
    (void)RedisSet(dataset_name, std::string{"entries:"} + std::string{entry_key}, value);
    return {true, "maria"};
}

StorageCommandResult StorageService::DatasetDelete(std::string_view dataset_name, std::string_view entry_key)
{
    MYSQL* maria = GetMariaForDataset(dataset_name);
    if (maria == nullptr)
    {
        return Failure("maria connection is unavailable");
    }
    if (entry_key.empty())
    {
        return Failure("entry_key must not be empty");
    }
    const auto ensure = EnsureMariaEntriesTable(dataset_name);
    if (!ensure.ok)
    {
        return ensure;
    }
    const std::string table = BuildMariaEntriesTableName(dataset_name);
    const std::string escaped_key = EscapeMariaLiteral(*maria, entry_key);
    const std::string sql = "DELETE FROM `" + table + "` WHERE `entry_key`='" + escaped_key + "'";
    if (mysql_query(maria, sql.c_str()) != 0)
    {
        return Failure(mysql_error(maria));
    }
    const auto redis = RedisDelete(dataset_name, std::string{"entries:"} + std::string{entry_key});
    if (!redis.ok)
    {
        return {true, "maria=OK redis=" + redis.message};
    }
    return {true, "OK"};
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

bool StorageService::DisableMariaTarget(std::string_view name)
{
    const auto it = mMariaConnections.find(std::string{name});
    if (it == mMariaConnections.end())
    {
        return false;
    }
    mDisabledMariaTargets.emplace(std::string{name});
    CloseMaria(it->second.handle);
    it->second.handle = nullptr;
    it->second.status.reachable = false;
    it->second.status.error = "disabled by runtime command";
    return true;
}

bool StorageService::EnableMariaTarget(std::string_view name)
{
    const auto it = mMariaConnections.find(std::string{name});
    if (it == mMariaConnections.end())
    {
        return false;
    }
    return mDisabledMariaTargets.erase(std::string{name}) > 0;
}

bool StorageService::IsMariaTargetDisabled(std::string_view name) const
{
    return mDisabledMariaTargets.contains(std::string{name});
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
    if (mDisabledMariaTargets.contains(name))
    {
        CloseMaria(state.handle);
        state.handle = nullptr;
        state.status.reachable = false;
        state.status.error = "disabled by runtime command";
        return;
    }
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
