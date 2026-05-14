#include "player_lease_service.h"

#include "../../framework/storage/connectivity_probe.h"

#include <hiredis/hiredis.h>

#include <chrono>
#include <sstream>

namespace
{
constexpr std::chrono::milliseconds kRenewInterval{2000};
constexpr std::string_view kRefreshScript =
    "if redis.call('GET', KEYS[1]) == ARGV[1] then "
    "return redis.call('PEXPIRE', KEYS[1], ARGV[2]) "
    "else return 0 end";
constexpr std::string_view kReleaseScript =
    "if redis.call('GET', KEYS[1]) == ARGV[1] then "
    "return redis.call('DEL', KEYS[1]) "
    "else return 0 end";
}

LifecycleTask PlayerLeaseService::Load()
{
    const auto it = mCommonConfiguration.redis.find(mRedisName);
    if (it == mCommonConfiguration.redis.end())
    {
        return LifecycleTask::Completed();
    }

    const auto result = some_server::storage::ConnectRedis(it->second);
    std::scoped_lock lock(mMutex);
    mRedis = result.context;
    mRedisKeyPrefix = it->second.key_prefix + "game:player:lease:";
    return LifecycleTask::Completed();
}

LifecycleTask PlayerLeaseService::Start()
{
    mStopping.store(false);
    {
        std::scoped_lock lock(mMutex);
        mSnapshot.running = true;
        mSnapshot.last_error.clear();
    }
    mThread = std::thread(&PlayerLeaseService::Loop, this);
    return LifecycleTask::Completed();
}

LifecycleTask PlayerLeaseService::Stop()
{
    mStopping.store(true);
    if (mThread.joinable())
    {
        mThread.join();
    }
    {
        std::scoped_lock lock(mMutex);
        ReleaseAllTrackedLocked();
        mSnapshot.running = false;
    }
    return LifecycleTask::Completed();
}

LifecycleTask PlayerLeaseService::Unload()
{
    std::scoped_lock lock(mMutex);
    some_server::storage::CloseRedis(mRedis);
    mRedis = nullptr;
    mRedisKeyPrefix.clear();
    mLeases.clear();
    return LifecycleTask::Completed();
}

ipc::Result PlayerLeaseService::Acquire(const std::uint64_t player_id)
{
    std::scoped_lock lock(mMutex);
    if (mRedis == nullptr)
    {
        return ipc::Result::Failure("redis player lease is unavailable");
    }
    if (mLeases.contains(player_id))
    {
        return ipc::Result::Success();
    }

    const PlayerLeaseOwner owner{
        .game_service_type = mGameServiceType,
        .game_instance_id = mGameInstanceId,
        .token = GenerateToken(player_id, mGameInstanceId)};
    const std::string key = BuildKey(player_id);
    const std::string value = BuildValue(owner);
    const auto ttl_ms = static_cast<std::uint64_t>(mLeaseTtlSeconds) * 1000;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(
            mRedis,
            "SET %b %b NX PX %llu",
            key.data(),
            key.size(),
            value.data(),
            value.size(),
            static_cast<unsigned long long>(ttl_ms)));
    if (reply == nullptr)
    {
        return ipc::Result::Failure(mRedis->err != 0 && mRedis->errstr[0] != '\0' ? mRedis->errstr : "redis SET failed");
    }

    const bool acquired =
        reply->type == REDIS_REPLY_STATUS && reply->str != nullptr && std::string_view(reply->str) == "OK";
    freeReplyObject(reply);
    if (!acquired)
    {
        return ipc::Result::Failure("player lease is already held by another game");
    }

    mLeases.emplace(player_id, LeaseRecord{.owner = owner});
    mSnapshot.tracked_player_count = mLeases.size();
    return ipc::Result::Success();
}

ipc::Result PlayerLeaseService::Release(const std::uint64_t player_id)
{
    std::scoped_lock lock(mMutex);
    const auto it = mLeases.find(player_id);
    if (it == mLeases.end())
    {
        return ipc::Result::Failure("player lease is not tracked locally");
    }

    std::string error;
    if (!ReleaseLeaseLocked(player_id, it->second.owner, error))
    {
        mSnapshot.last_error = std::move(error);
        ++mSnapshot.release_failure_count;
    }
    else
    {
        ++mSnapshot.release_success_count;
    }
    mLeases.erase(it);
    mSnapshot.tracked_player_count = mLeases.size();
    return ipc::Result::Success();
}

std::optional<PlayerLeaseOwner> PlayerLeaseService::Lookup(const std::uint64_t player_id)
{
    std::scoped_lock lock(mMutex);
    if (mRedis == nullptr)
    {
        return std::nullopt;
    }

    const std::string key = BuildKey(player_id);
    redisReply* reply = static_cast<redisReply*>(redisCommand(mRedis, "GET %b", key.data(), key.size()));
    if (reply == nullptr)
    {
        return std::nullopt;
    }

    std::optional<PlayerLeaseOwner> owner;
    if (reply->type == REDIS_REPLY_STRING && reply->str != nullptr)
    {
        owner = ParseValue(std::string_view{reply->str, static_cast<std::size_t>(reply->len)});
    }
    freeReplyObject(reply);
    return owner;
}

bool PlayerLeaseService::IsHeldLocally(const std::uint64_t player_id) const
{
    std::scoped_lock lock(mMutex);
    return mLeases.contains(player_id);
}

std::vector<std::uint64_t> PlayerLeaseService::TrackedPlayerIds() const
{
    std::scoped_lock lock(mMutex);
    std::vector<std::uint64_t> players;
    players.reserve(mLeases.size());
    for (const auto& [player_id, _] : mLeases)
    {
        players.push_back(player_id);
    }
    return players;
}

PlayerLeaseSnapshot PlayerLeaseService::Snapshot() const
{
    std::scoped_lock lock(mMutex);
    auto snapshot = mSnapshot;
    snapshot.tracked_player_count = mLeases.size();
    return snapshot;
}

PlayerLeasePlayerSnapshot PlayerLeaseService::PlayerSnapshot(const std::uint64_t player_id) const
{
    std::scoped_lock lock(mMutex);
    PlayerLeasePlayerSnapshot snapshot;
    snapshot.player_id = player_id;
    const auto it = mLeases.find(player_id);
    if (it == mLeases.end())
    {
        return snapshot;
    }

    snapshot.tracked = true;
    snapshot.game_service_type = it->second.owner.game_service_type;
    snapshot.game_instance_id = it->second.owner.game_instance_id;
    snapshot.token = it->second.owner.token;
    return snapshot;
}

std::uint64_t PlayerLeaseService::NowMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string PlayerLeaseService::BuildKey(const std::uint64_t player_id) const
{
    return mRedisKeyPrefix + std::to_string(player_id);
}

std::string PlayerLeaseService::BuildValue(const PlayerLeaseOwner& owner)
{
    return std::to_string(owner.game_service_type) + ":" + std::to_string(owner.game_instance_id) + ":" + owner.token;
}

std::optional<PlayerLeaseOwner> PlayerLeaseService::ParseValue(const std::string_view value)
{
    std::istringstream stream(std::string{value});
    std::string service_type_text;
    std::string instance_id_text;
    std::string token;
    if (!std::getline(stream, service_type_text, ':') || !std::getline(stream, instance_id_text, ':') ||
        !std::getline(stream, token, ':'))
    {
        return std::nullopt;
    }

    try
    {
        return PlayerLeaseOwner{
            .game_service_type = static_cast<std::uint32_t>(std::stoul(service_type_text)),
            .game_instance_id = static_cast<std::uint32_t>(std::stoul(instance_id_text)),
            .token = token};
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::string PlayerLeaseService::GenerateToken(const std::uint64_t player_id, const std::uint32_t instance_id)
{
    return std::to_string(player_id) + "-" + std::to_string(instance_id) + "-" + std::to_string(NowMs());
}

bool PlayerLeaseService::RefreshLeaseLocked(
    const std::uint64_t player_id,
    const PlayerLeaseOwner& owner,
    std::string& error)
{
    if (mRedis == nullptr)
    {
        error = "redis player lease is unavailable";
        return false;
    }

    const std::string key = BuildKey(player_id);
    const std::string value = BuildValue(owner);
    const auto ttl_ms = static_cast<std::uint64_t>(mLeaseTtlSeconds) * 1000;
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(
            mRedis,
            "EVAL %b 1 %b %b %llu",
            kRefreshScript.data(),
            kRefreshScript.size(),
            key.data(),
            key.size(),
            value.data(),
            value.size(),
            static_cast<unsigned long long>(ttl_ms)));
    if (reply == nullptr)
    {
        error = mRedis->err != 0 && mRedis->errstr[0] != '\0' ? mRedis->errstr : "redis EVAL failed";
        return false;
    }

    const bool ok = reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    freeReplyObject(reply);
    if (!ok)
    {
        error = "player lease refresh rejected";
    }
    return ok;
}

bool PlayerLeaseService::ReleaseLeaseLocked(
    const std::uint64_t player_id,
    const PlayerLeaseOwner& owner,
    std::string& error)
{
    if (mRedis == nullptr)
    {
        error = "redis player lease is unavailable";
        return false;
    }

    const std::string key = BuildKey(player_id);
    const std::string value = BuildValue(owner);
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(
            mRedis,
            "EVAL %b 1 %b %b",
            kReleaseScript.data(),
            kReleaseScript.size(),
            key.data(),
            key.size(),
            value.data(),
            value.size()));
    if (reply == nullptr)
    {
        error = mRedis->err != 0 && mRedis->errstr[0] != '\0' ? mRedis->errstr : "redis EVAL failed";
        return false;
    }

    const bool ok = reply->type == REDIS_REPLY_INTEGER;
    freeReplyObject(reply);
    if (!ok)
    {
        error = "player lease release rejected";
    }
    return ok;
}

void PlayerLeaseService::ReleaseAllTrackedLocked()
{
    for (const auto& [player_id, lease] : mLeases)
    {
        std::string error;
        if (!ReleaseLeaseLocked(player_id, lease.owner, error))
        {
            ++mSnapshot.release_failure_count;
            mSnapshot.last_error = std::move(error);
            continue;
        }
        ++mSnapshot.release_success_count;
    }
    mLeases.clear();
    mSnapshot.tracked_player_count = 0;
}

void PlayerLeaseService::Loop()
{
    while (!mStopping.load())
    {
        {
            std::scoped_lock lock(mMutex);
            mSnapshot.last_renew_ms = NowMs();
            std::vector<std::uint64_t> lost_players;
            for (const auto& [player_id, lease] : mLeases)
            {
                std::string error;
                if (RefreshLeaseLocked(player_id, lease.owner, error))
                {
                    ++mSnapshot.renew_success_count;
                    continue;
                }
                ++mSnapshot.renew_failure_count;
                mSnapshot.last_error = std::move(error);
                lost_players.push_back(player_id);
            }
            for (const auto player_id : lost_players)
            {
                mLeases.erase(player_id);
                ++mSnapshot.local_loss_count;
            }
            mSnapshot.tracked_player_count = mLeases.size();
        }
        std::this_thread::sleep_for(kRenewInterval);
    }
}
