#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/storage/resolved_configuration.h"
#include "player_lease_service.h"
#include "player_repository.h"
#include "player_session_service.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

struct PlayerPersistenceSnapshot
{
    bool running = false;
    std::uint64_t scan_count = 0;
    std::uint64_t tracked_player_count = 0;
    std::uint64_t pending_initial_persist_count = 0;
    std::uint64_t created_without_maria_count = 0;
    std::uint64_t flush_attempt_count = 0;
    std::uint64_t flush_success_count = 0;
    std::uint64_t flush_failure_count = 0;
    std::uint64_t last_scan_ms = 0;
    std::uint64_t last_flush_player_id = 0;
    std::uint64_t last_flush_ms = 0;
    std::string last_error;
};

struct PlayerPersistencePlayerSnapshot
{
    bool tracked = false;
    std::uint64_t player_id = 0;
    bool repository_present = false;
    bool loaded = false;
    bool dirty = false;
    bool pending_initial_persist = false;
    bool created_without_maria = false;
    std::uint64_t flush_count = 0;
    std::uint64_t last_dirty_ms = 0;
    std::uint64_t last_flush_ms = 0;
    std::uint64_t consecutive_failure_count = 0;
    std::uint64_t next_retry_ms = 0;
    std::uint64_t last_failure_ms = 0;
    std::string last_error;
};

class PlayerPersistenceService final : public ServiceBase
{
public:
    PlayerPersistenceService(
        const some_server::storage::ResolvedStorageConfiguration& storage_configuration,
        PlayerLeaseService* lease_service,
        PlayerRepository* repository,
        PlayerSessionService* session_service)
        : ServiceBase("player_persistence", 35)
        , mStorageConfiguration(storage_configuration)
        , mLeaseService(lease_service)
        , mRepository(repository)
        , mSessionService(session_service)
    {
    }

    LifecycleTask Start() override;
    LifecycleTask Stop() override;
    PlayerPersistenceSnapshot Snapshot() const;
    PlayerPersistencePlayerSnapshot PlayerSnapshot(std::uint64_t player_id) const;
    std::size_t FlushDuePlayersOnce();
    std::size_t ResetRetryStates();
    bool ResetRetryState(std::uint64_t player_id);

private:
    struct RetryState
    {
        std::uint64_t consecutive_failure_count = 0;
        std::uint64_t next_retry_ms = 0;
        std::uint64_t last_failure_ms = 0;
        std::string last_error;
    };

    static std::uint64_t NowMs();
    std::uint64_t ComputeRetryDelayMs(std::uint64_t consecutive_failure_count) const;
    bool ShouldFlush(
        std::uint64_t player_id,
        const PlayerRepositoryRecord& repository,
        const std::optional<PlayerSessionRecord>& session,
        std::uint64_t now_ms) const;
    void Loop();

    const some_server::storage::ResolvedStorageConfiguration& mStorageConfiguration;
    PlayerLeaseService* mLeaseService = nullptr;
    PlayerRepository* mRepository = nullptr;
    PlayerSessionService* mSessionService = nullptr;
    std::atomic<bool> mStopping = false;
    std::thread mThread;
    mutable std::mutex mMutex;
    PlayerPersistenceSnapshot mSnapshot;
    std::unordered_map<std::uint64_t, RetryState> mRetryStates;
};
