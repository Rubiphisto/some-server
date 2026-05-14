#pragma once

#include "../../framework/application/configuration.h"
#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/result.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct redisContext;

struct PlayerLeaseOwner
{
    std::uint32_t game_service_type = 0;
    std::uint32_t game_instance_id = 0;
    std::string token;
};

struct PlayerLeaseSnapshot
{
    bool running = false;
    std::uint64_t tracked_player_count = 0;
    std::uint64_t renew_success_count = 0;
    std::uint64_t renew_failure_count = 0;
    std::uint64_t local_loss_count = 0;
    std::uint64_t release_success_count = 0;
    std::uint64_t release_failure_count = 0;
    std::uint64_t last_renew_ms = 0;
    std::string last_error;
};

struct PlayerLeasePlayerSnapshot
{
    bool tracked = false;
    std::uint64_t player_id = 0;
    std::uint32_t game_service_type = 0;
    std::uint32_t game_instance_id = 0;
    std::string token;
};

class PlayerLeaseService final : public ServiceBase
{
public:
    PlayerLeaseService(
        const CommonConfiguration& common_configuration,
        std::string redis_name,
        std::uint32_t game_service_type,
        std::uint32_t game_instance_id,
        std::uint32_t lease_ttl_seconds)
        : ServiceBase("player_lease", 18)
        , mCommonConfiguration(common_configuration)
        , mRedisName(std::move(redis_name))
        , mGameServiceType(game_service_type)
        , mGameInstanceId(game_instance_id)
        , mLeaseTtlSeconds(lease_ttl_seconds)
    {
    }

    LifecycleTask Load() override;
    LifecycleTask Start() override;
    LifecycleTask Stop() override;
    LifecycleTask Unload() override;

    ipc::Result Acquire(std::uint64_t player_id);
    ipc::Result Release(std::uint64_t player_id);
    std::optional<PlayerLeaseOwner> Lookup(std::uint64_t player_id);
    bool IsHeldLocally(std::uint64_t player_id) const;
    std::vector<std::uint64_t> TrackedPlayerIds() const;

    PlayerLeaseSnapshot Snapshot() const;
    PlayerLeasePlayerSnapshot PlayerSnapshot(std::uint64_t player_id) const;

private:
    struct LeaseRecord
    {
        PlayerLeaseOwner owner;
    };

    static std::uint64_t NowMs();
    std::string BuildKey(std::uint64_t player_id) const;
    static std::string BuildValue(const PlayerLeaseOwner& owner);
    static std::optional<PlayerLeaseOwner> ParseValue(std::string_view value);
    static std::string GenerateToken(std::uint64_t player_id, std::uint32_t instance_id);
    bool RefreshLeaseLocked(std::uint64_t player_id, const PlayerLeaseOwner& owner, std::string& error);
    bool ReleaseLeaseLocked(std::uint64_t player_id, const PlayerLeaseOwner& owner, std::string& error);
    void ReleaseAllTrackedLocked();
    void Loop();

    const CommonConfiguration& mCommonConfiguration;
    std::string mRedisName;
    std::uint32_t mGameServiceType = 0;
    std::uint32_t mGameInstanceId = 0;
    std::uint32_t mLeaseTtlSeconds = 0;
    std::string mRedisKeyPrefix;
    std::atomic<bool> mStopping = false;
    std::thread mThread;
    mutable std::mutex mMutex;
    redisContext* mRedis = nullptr;
    std::unordered_map<std::uint64_t, LeaseRecord> mLeases;
    PlayerLeaseSnapshot mSnapshot;
};
