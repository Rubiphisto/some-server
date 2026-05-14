#include "account_directory_service.h"

#include "../../framework/storage/connectivity_probe.h"

#include <hiredis/hiredis.h>

#include <sstream>

LifecycleTask GateAccountDirectoryService::Load()
{
    const auto it = mCommonConfiguration.redis.find(mRedisName);
    if (it == mCommonConfiguration.redis.end())
    {
        return LifecycleTask::Completed();
    }

    const auto result = some_server::storage::ConnectRedis(it->second);
    std::scoped_lock lock(mMutex);
    mRedis = result.context;
    mRedisKeyPrefix = it->second.key_prefix + "gate:account:";
    return LifecycleTask::Completed();
}

LifecycleTask GateAccountDirectoryService::Unload()
{
    std::scoped_lock lock(mMutex);
    some_server::storage::CloseRedis(mRedis);
    mRedis = nullptr;
    mRedisKeyPrefix.clear();
    return LifecycleTask::Completed();
}

std::optional<GateAccountOwner> GateAccountDirectoryService::LookupAccount(const std::string_view account_id)
{
    std::scoped_lock lock(mMutex);
    if (mRedis == nullptr)
    {
        return std::nullopt;
    }

    const std::string key = mRedisKeyPrefix + std::string{account_id};
    redisReply* reply = static_cast<redisReply*>(redisCommand(mRedis, "GET %b", key.data(), key.size()));
    if (reply == nullptr)
    {
        return std::nullopt;
    }

    std::optional<GateAccountOwner> owner;
    if (reply->type == REDIS_REPLY_STRING && reply->str != nullptr)
    {
        owner = ParseValue(std::string_view{reply->str, reply->len});
    }
    freeReplyObject(reply);
    return owner;
}

ipc::Result GateAccountDirectoryService::BindAccount(const std::string_view account_id, const GateAccountOwner& owner)
{
    std::scoped_lock lock(mMutex);
    if (mRedis == nullptr)
    {
        return ipc::Result::Failure("redis account directory is unavailable");
    }

    const std::string key = mRedisKeyPrefix + std::string{account_id};
    const std::string value = BuildValue(owner);
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(mRedis, "SET %b %b", key.data(), key.size(), value.data(), value.size()));
    if (reply == nullptr)
    {
        return ipc::Result::Failure(mRedis->err != 0 && mRedis->errstr[0] != '\0' ? mRedis->errstr : "redis SET failed");
    }
    freeReplyObject(reply);
    return ipc::Result::Success();
}

ipc::Result GateAccountDirectoryService::RemoveAccountIfMatches(
    const std::string_view account_id,
    const GateAccountOwner& owner)
{
    std::scoped_lock lock(mMutex);
    if (mRedis == nullptr)
    {
        return ipc::Result::Failure("redis account directory is unavailable");
    }

    const std::string key = mRedisKeyPrefix + std::string{account_id};
    redisReply* get_reply = static_cast<redisReply*>(redisCommand(mRedis, "GET %b", key.data(), key.size()));
    if (get_reply == nullptr)
    {
        return ipc::Result::Failure(mRedis->err != 0 && mRedis->errstr[0] != '\0' ? mRedis->errstr : "redis GET failed");
    }

    bool should_delete = false;
    if (get_reply->type == REDIS_REPLY_STRING && get_reply->str != nullptr)
    {
        const auto existing = ParseValue(std::string_view{get_reply->str, get_reply->len});
        should_delete = existing.has_value() &&
            existing->gate_service_type == owner.gate_service_type &&
            existing->gate_instance_id == owner.gate_instance_id &&
            existing->gate_session_id == owner.gate_session_id;
    }
    freeReplyObject(get_reply);

    if (!should_delete)
    {
        return ipc::Result::Success();
    }

    redisReply* del_reply = static_cast<redisReply*>(redisCommand(mRedis, "DEL %b", key.data(), key.size()));
    if (del_reply == nullptr)
    {
        return ipc::Result::Failure(mRedis->err != 0 && mRedis->errstr[0] != '\0' ? mRedis->errstr : "redis DEL failed");
    }
    freeReplyObject(del_reply);
    return ipc::Result::Success();
}

std::string GateAccountDirectoryService::BuildValue(const GateAccountOwner& owner)
{
    return std::to_string(owner.gate_service_type) + ":" + std::to_string(owner.gate_instance_id) + ":" +
        std::to_string(owner.gate_session_id);
}

std::optional<GateAccountOwner> GateAccountDirectoryService::ParseValue(const std::string_view value)
{
    std::istringstream stream(std::string{value});
    std::string service_type_text;
    std::string instance_id_text;
    std::string gate_session_id_text;
    if (!std::getline(stream, service_type_text, ':') || !std::getline(stream, instance_id_text, ':') ||
        !std::getline(stream, gate_session_id_text, ':'))
    {
        return std::nullopt;
    }

    try
    {
        return GateAccountOwner{
            .gate_service_type = static_cast<std::uint32_t>(std::stoul(service_type_text)),
            .gate_instance_id = static_cast<std::uint32_t>(std::stoul(instance_id_text)),
            .gate_session_id = std::stoull(gate_session_id_text)};
    }
    catch (...)
    {
        return std::nullopt;
    }
}
