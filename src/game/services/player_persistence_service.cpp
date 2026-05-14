#include "player_persistence_service.h"

#include <chrono>
#include <unordered_map>

namespace
{
constexpr std::chrono::milliseconds kLoopInterval{1000};
constexpr std::uint64_t kMaxBackoffMultiplier = 8;
}

LifecycleTask PlayerPersistenceService::Start()
{
    if (mLeaseService == nullptr || mRepository == nullptr || mSessionService == nullptr)
    {
        return LifecycleTask::Completed();
    }

    mStopping.store(false);
    {
        std::scoped_lock lock(mMutex);
        mSnapshot.running = true;
        mSnapshot.last_error.clear();
    }
    mThread = std::thread(&PlayerPersistenceService::Loop, this);
    return LifecycleTask::Completed();
}

LifecycleTask PlayerPersistenceService::Stop()
{
    mStopping.store(true);
    if (mThread.joinable())
    {
        mThread.join();
    }
    {
        std::scoped_lock lock(mMutex);
        mSnapshot.running = false;
    }
    return LifecycleTask::Completed();
}

PlayerPersistenceSnapshot PlayerPersistenceService::Snapshot() const
{
    std::scoped_lock lock(mMutex);
    return mSnapshot;
}

PlayerPersistencePlayerSnapshot PlayerPersistenceService::PlayerSnapshot(const std::uint64_t player_id) const
{
    PlayerPersistencePlayerSnapshot snapshot;
    snapshot.player_id = player_id;
    if (mRepository != nullptr)
    {
        const auto repository = mRepository->Snapshot(player_id);
        if (repository.has_value())
        {
            snapshot.repository_present = true;
            snapshot.loaded = repository->loaded;
            snapshot.dirty = repository->dirty;
            snapshot.pending_initial_persist = repository->pending_initial_persist;
            snapshot.created_without_maria = repository->created_without_maria;
            snapshot.flush_count = repository->flush_count;
            snapshot.last_dirty_ms = repository->last_dirty_ms;
            snapshot.last_flush_ms = repository->last_flush_ms;
        }
    }

    std::scoped_lock lock(mMutex);
    const auto it = mRetryStates.find(player_id);
    if (it == mRetryStates.end())
    {
        return snapshot;
    }

    snapshot.tracked = true;
    snapshot.consecutive_failure_count = it->second.consecutive_failure_count;
    snapshot.next_retry_ms = it->second.next_retry_ms;
    snapshot.last_failure_ms = it->second.last_failure_ms;
    snapshot.last_error = it->second.last_error;
    return snapshot;
}

std::size_t PlayerPersistenceService::FlushDuePlayersOnce()
{
    if (mLeaseService == nullptr || mRepository == nullptr || mSessionService == nullptr)
    {
        return 0;
    }

    const auto now_ms = NowMs();
    std::uint64_t tracked_player_count = 0;
    std::uint64_t pending_initial_persist_count = 0;
    std::uint64_t created_without_maria_count = 0;
    {
        std::scoped_lock lock(mMutex);
        ++mSnapshot.scan_count;
        mSnapshot.last_scan_ms = now_ms;
    }

    std::size_t flushed = 0;
    for (const auto player_id : mRepository->LoadedPlayerIds())
    {
        const auto repository = mRepository->Snapshot(player_id);
        if (!repository.has_value())
        {
            continue;
        }
        ++tracked_player_count;
        if (repository->pending_initial_persist)
        {
            ++pending_initial_persist_count;
        }
        if (repository->created_without_maria)
        {
            ++created_without_maria_count;
        }
        if (!mLeaseService->IsHeldLocally(player_id))
        {
            continue;
        }

        const auto session = mSessionService->Snapshot(player_id);
        if (!ShouldFlush(player_id, *repository, session, now_ms))
        {
            continue;
        }

        {
            std::scoped_lock lock(mMutex);
            ++mSnapshot.flush_attempt_count;
            mSnapshot.last_flush_player_id = player_id;
            mSnapshot.last_flush_ms = now_ms;
        }

        const auto flush = mRepository->FlushPlayer(player_id);
        std::scoped_lock lock(mMutex);
        if (!flush.ok)
        {
            ++mSnapshot.flush_failure_count;
            mSnapshot.last_error = flush.message;
            auto& retry = mRetryStates[player_id];
            ++retry.consecutive_failure_count;
            retry.last_failure_ms = now_ms;
            retry.next_retry_ms = now_ms + ComputeRetryDelayMs(retry.consecutive_failure_count);
            retry.last_error = flush.message;
            continue;
        }
        mRetryStates.erase(player_id);
        if (repository->pending_initial_persist && pending_initial_persist_count > 0)
        {
            --pending_initial_persist_count;
        }
        if (repository->created_without_maria && created_without_maria_count > 0)
        {
            --created_without_maria_count;
        }
        ++mSnapshot.flush_success_count;
        ++flushed;
    }

    {
        std::scoped_lock lock(mMutex);
        mSnapshot.tracked_player_count = tracked_player_count;
        mSnapshot.pending_initial_persist_count = pending_initial_persist_count;
        mSnapshot.created_without_maria_count = created_without_maria_count;
    }

    return flushed;
}

std::size_t PlayerPersistenceService::ResetRetryStates()
{
    std::scoped_lock lock(mMutex);
    const auto count = mRetryStates.size();
    mRetryStates.clear();
    mSnapshot.last_error.clear();
    return count;
}

bool PlayerPersistenceService::ResetRetryState(const std::uint64_t player_id)
{
    std::scoped_lock lock(mMutex);
    return mRetryStates.erase(player_id) > 0;
}

std::uint64_t PlayerPersistenceService::NowMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::uint64_t PlayerPersistenceService::ComputeRetryDelayMs(const std::uint64_t consecutive_failure_count) const
{
    const auto* dataset = mStorageConfiguration.FindDataset("player");
    if (dataset == nullptr)
    {
        return 5000;
    }

    const std::uint64_t base_ms =
        static_cast<std::uint64_t>(dataset->timing.landing_min_time_seconds) * 1000;
    const std::uint64_t max_ms =
        static_cast<std::uint64_t>(dataset->timing.landing_time_seconds) * 1000;

    std::uint64_t multiplier = 1;
    for (std::uint64_t i = 1; i < consecutive_failure_count && multiplier < kMaxBackoffMultiplier; ++i)
    {
        multiplier *= 2;
    }

    const std::uint64_t delay_ms = base_ms * multiplier;
    return delay_ms < max_ms ? delay_ms : max_ms;
}

bool PlayerPersistenceService::ShouldFlush(
    const std::uint64_t player_id,
    const PlayerRepositoryRecord& repository,
    const std::optional<PlayerSessionRecord>& session,
    const std::uint64_t now_ms) const
{
    if (!repository.loaded || !repository.dirty)
    {
        return false;
    }

    if (repository.pending_initial_persist)
    {
        return true;
    }

    const auto* dataset = mStorageConfiguration.FindDataset("player");
    if (dataset == nullptr)
    {
        return false;
    }

    {
        std::scoped_lock lock(mMutex);
        const auto retry = mRetryStates.find(player_id);
        if (retry != mRetryStates.end() && retry->second.next_retry_ms > now_ms)
        {
            return false;
        }
    }

    if (session.has_value() && session->state == PlayerSessionState::detached)
    {
        return true;
    }

    if (repository.last_dirty_ms == 0)
    {
        return true;
    }

    const auto min_flush_age_ms =
        static_cast<std::uint64_t>(dataset->timing.landing_min_time_seconds) * 1000;
    return now_ms >= repository.last_dirty_ms && (now_ms - repository.last_dirty_ms) >= min_flush_age_ms;
}

void PlayerPersistenceService::Loop()
{
    while (!mStopping.load())
    {
        (void)FlushDuePlayersOnce();
        std::this_thread::sleep_for(kLoopInterval);
    }
}
