#include "player_session_service.h"

#include "ipc_client_service.h"
#include "player_lease_service.h"
#include "player_runtime_service.h"

#include <ipc/session.pb.h>

#include <chrono>
#include <thread>
#include <vector>

namespace
{
constexpr std::chrono::milliseconds kSessionReconcileInterval{1000};
}

const char* ToString(const PlayerSessionState state)
{
    switch (state)
    {
    case PlayerSessionState::loading:
        return "loading";
    case PlayerSessionState::online:
        return "online";
    case PlayerSessionState::detached:
        return "detached";
    case PlayerSessionState::unloading:
        return "unloading";
    }
    return "unknown";
}

ipc::Result PlayerSessionService::ActivatePlayer(
    const std::uint64_t player_id,
    const std::uint32_t gate_service_type,
    const std::uint32_t gate_instance_id,
    const std::uint64_t gate_session_id,
    const bool allow_create_without_maria)
{
    if (mRepository == nullptr || mLeaseService == nullptr || mRuntimeService == nullptr || mIpcService == nullptr)
    {
        return ipc::Result::Failure("player session dependencies are not registered");
    }

    bool reuse_existing = false;
    {
        std::scoped_lock lock(mMutex);
        if (const auto it = mPlayers.find(player_id); it != mPlayers.end())
        {
            if ((it->second.state == PlayerSessionState::online || it->second.state == PlayerSessionState::detached) &&
                !mLeaseService->IsHeldLocally(player_id))
            {
                reuse_existing = true;
            }
            else
            {
                if (it->second.state == PlayerSessionState::online)
                {
                    it->second.gate_service_type = gate_service_type;
                    it->second.gate_instance_id = gate_instance_id;
                    it->second.gate_session_id = gate_session_id;
                    it->second.last_state_change_ms = NowMs();
                    ++it->second.activate_count;
                    return ipc::Result::Success();
                }
                if (it->second.state == PlayerSessionState::detached)
                {
                    it->second.state = PlayerSessionState::online;
                    it->second.gate_service_type = gate_service_type;
                    it->second.gate_instance_id = gate_instance_id;
                    it->second.gate_session_id = gate_session_id;
                    it->second.last_state_change_ms = NowMs();
                    it->second.detach_deadline_ms = 0;
                    ++it->second.activate_count;
                    return ipc::Result::Success();
                }
            }

            if (it->second.state == PlayerSessionState::loading || it->second.state == PlayerSessionState::unloading)
            {
                return ipc::Result::Failure("player is transitioning and cannot be activated");
            }
        }
    }

    if (const auto lease_result = mLeaseService->Acquire(player_id); !lease_result.ok)
    {
        return lease_result;
    }

    if (reuse_existing)
    {
        if (!mRepository->Snapshot(player_id).has_value())
        {
            if (const auto load_result = mRepository->LoadPlayer(player_id, allow_create_without_maria); !load_result.ok)
            {
                (void)mLeaseService->Release(player_id);
                return load_result;
            }
        }

        const auto bind_result = mIpcService->BindLocalPlayer(player_id);
        if (!bind_result.ok && bind_result.message != "player is already bound locally")
        {
            (void)mLeaseService->Release(player_id);
            return bind_result;
        }
        if (const auto ensure_runtime = mRuntimeService->EnsurePlayer(player_id); !ensure_runtime.ok)
        {
            (void)mLeaseService->Release(player_id);
            (void)mIpcService->UnbindLocalPlayer(player_id);
            return ensure_runtime;
        }

        std::scoped_lock lock(mMutex);
        const auto it = mPlayers.find(player_id);
        if (it == mPlayers.end())
        {
            (void)mLeaseService->Release(player_id);
            return ipc::Result::Failure("player session disappeared during lease recovery");
        }
        if (it->second.state == PlayerSessionState::loading || it->second.state == PlayerSessionState::unloading)
        {
            (void)mLeaseService->Release(player_id);
            return ipc::Result::Failure("player is transitioning and cannot be activated");
        }

        it->second.state = PlayerSessionState::online;
        it->second.gate_service_type = gate_service_type;
        it->second.gate_instance_id = gate_instance_id;
        it->second.gate_session_id = gate_session_id;
        it->second.last_state_change_ms = NowMs();
        it->second.detach_deadline_ms = 0;
        ++it->second.activate_count;
        return ipc::Result::Success();
    }

    if (const auto load_result = mRepository->LoadPlayer(player_id, allow_create_without_maria); !load_result.ok)
    {
        (void)mLeaseService->Release(player_id);
        return load_result;
    }

    const auto bind_result = mIpcService->BindLocalPlayer(player_id);
    if (!bind_result.ok && bind_result.message != "player is already bound locally")
    {
        (void)mLeaseService->Release(player_id);
        (void)mRepository->ReleasePlayer(player_id);
        return bind_result;
    }
    if (const auto ensure_runtime = mRuntimeService->EnsurePlayer(player_id); !ensure_runtime.ok)
    {
        (void)mLeaseService->Release(player_id);
        (void)mIpcService->UnbindLocalPlayer(player_id);
        (void)mRepository->ReleasePlayer(player_id);
        return ensure_runtime;
    }

    std::scoped_lock lock(mMutex);
    auto& record = mPlayers[player_id];
    record.state = PlayerSessionState::online;
    record.gate_service_type = gate_service_type;
    record.gate_instance_id = gate_instance_id;
    record.gate_session_id = gate_session_id;
    record.last_state_change_ms = NowMs();
    record.detach_deadline_ms = 0;
    ++record.activate_count;
    return ipc::Result::Success();
}

LifecycleTask PlayerSessionService::Start()
{
    mStopping.store(false);
    mThread = std::thread(&PlayerSessionService::Loop, this);
    return LifecycleTask::Completed();
}

LifecycleTask PlayerSessionService::Stop()
{
    mStopping.store(true);
    if (mThread.joinable())
    {
        mThread.join();
    }
    return LifecycleTask::Completed();
}

ipc::Result PlayerSessionService::DetachPlayer(const std::uint64_t player_id)
{
    std::scoped_lock lock(mMutex);
    const auto it = mPlayers.find(player_id);
    if (it == mPlayers.end())
    {
        return ipc::Result::Failure("player session is not tracked");
    }
    if (it->second.state != PlayerSessionState::online)
    {
        return ipc::Result::Failure("player is not online");
    }

    it->second.state = PlayerSessionState::detached;
    it->second.last_state_change_ms = NowMs();
    it->second.detach_deadline_ms =
        it->second.last_state_change_ms + static_cast<std::uint64_t>(mDetachedTtlSeconds) * 1000;
    it->second.gate_service_type = 0;
    it->second.gate_instance_id = 0;
    it->second.gate_session_id = 0;
    return ipc::Result::Success();
}

ipc::Result PlayerSessionService::DetachPlayer(
    const std::uint64_t player_id,
    const std::uint32_t gate_service_type,
    const std::uint32_t gate_instance_id,
    const std::uint64_t gate_session_id)
{
    std::scoped_lock lock(mMutex);
    const auto it = mPlayers.find(player_id);
    if (it == mPlayers.end())
    {
        return ipc::Result::Failure("player session is not tracked");
    }
    if (it->second.state != PlayerSessionState::online)
    {
        return ipc::Result::Failure("player is not online");
    }
    if (it->second.gate_service_type != gate_service_type || it->second.gate_instance_id != gate_instance_id ||
        it->second.gate_session_id != gate_session_id)
    {
        return ipc::Result::Failure("player gate session does not match");
    }

    it->second.state = PlayerSessionState::detached;
    it->second.last_state_change_ms = NowMs();
    it->second.detach_deadline_ms =
        it->second.last_state_change_ms + static_cast<std::uint64_t>(mDetachedTtlSeconds) * 1000;
    it->second.gate_service_type = 0;
    it->second.gate_instance_id = 0;
    it->second.gate_session_id = 0;
    return ipc::Result::Success();
}

ipc::Result PlayerSessionService::ReleasePlayer(const std::uint64_t player_id)
{
    if (mRepository == nullptr || mLeaseService == nullptr || mRuntimeService == nullptr || mIpcService == nullptr)
    {
        return ipc::Result::Failure("player session dependencies are not registered");
    }

    PlayerSessionRecord previous_record;
    bool has_dirty_repository = false;
    {
        std::scoped_lock lock(mMutex);
        const auto it = mPlayers.find(player_id);
        if (it == mPlayers.end())
        {
            return ipc::Result::Failure("player session is not tracked");
        }
        previous_record = it->second;
        it->second.state = PlayerSessionState::unloading;
        it->second.last_state_change_ms = NowMs();
    }

    if (const auto repository = mRepository->Snapshot(player_id); repository.has_value())
    {
        has_dirty_repository = repository->dirty;
    }
    if (has_dirty_repository)
    {
        const auto flush_result = mRepository->FlushPlayer(player_id);
        if (!flush_result.ok)
        {
            std::scoped_lock lock(mMutex);
            const auto it = mPlayers.find(player_id);
            if (it != mPlayers.end())
            {
                it->second = previous_record;
            }
            return ipc::Result::Failure("player flush before release failed: " + flush_result.message);
        }
    }
    if (const auto unbind_result = mIpcService->UnbindLocalPlayer(player_id); !unbind_result.ok)
    {
        return unbind_result;
    }
    (void)mRuntimeService->RemovePlayer(player_id);
    if (const auto release_result = mRepository->ReleasePlayer(player_id); !release_result.ok)
    {
        return release_result;
    }
    (void)mLeaseService->Release(player_id);

    std::scoped_lock lock(mMutex);
    mPlayers.erase(player_id);
    return ipc::Result::Success();
}

std::size_t PlayerSessionService::ExpireDetachedPlayers()
{
    std::vector<std::uint64_t> expired_players;
    const auto now = NowMs();
    {
        std::scoped_lock lock(mMutex);
        for (const auto& [player_id, record] : mPlayers)
        {
            if (record.state == PlayerSessionState::detached && record.detach_deadline_ms != 0 &&
                record.detach_deadline_ms <= now)
            {
                expired_players.push_back(player_id);
            }
        }
    }

    std::size_t released = 0;
    for (const auto player_id : expired_players)
    {
        if (ReleasePlayer(player_id).ok)
        {
            ++released;
        }
    }
    return released;
}

std::size_t PlayerSessionService::ReconcileLeaseLosses()
{
    if (mLeaseService == nullptr || mIpcService == nullptr)
    {
        return 0;
    }

    struct LostPlayerAction
    {
        std::uint64_t player_id = 0;
        PlayerSessionState state = PlayerSessionState::detached;
        std::uint32_t gate_service_type = 0;
        std::uint32_t gate_instance_id = 0;
        std::uint64_t gate_session_id = 0;
    };

    std::vector<LostPlayerAction> lost_players;
    {
        std::scoped_lock lock(mMutex);
        for (const auto& [player_id, record] : mPlayers)
        {
            if ((record.state == PlayerSessionState::online || record.state == PlayerSessionState::detached) &&
                !mLeaseService->IsHeldLocally(player_id))
            {
                lost_players.push_back(LostPlayerAction{
                    .player_id = player_id,
                    .state = record.state,
                    .gate_service_type = record.gate_service_type,
                    .gate_instance_id = record.gate_instance_id,
                    .gate_session_id = record.gate_session_id});
            }
        }
    }

    const auto now = NowMs();
    std::size_t reconciled = 0;
    for (const auto& action : lost_players)
    {
        if (action.state == PlayerSessionState::online && action.gate_service_type != 0 && action.gate_instance_id != 0 &&
            action.gate_session_id != 0)
        {
            pb::ipc::UnbindPlayerSession request;
            request.set_player_id(action.player_id);
            request.set_gate_service_type(action.gate_service_type);
            request.set_gate_instance_id(action.gate_instance_id);
            request.set_gate_session_id(action.gate_session_id);
            request.set_reason(pb::ipc::DISCONNECT_REASON_KICKED);
            (void)mIpcService->SendProcessPayload(
                ipc::ProcessId{
                    .service_type = action.gate_service_type,
                    .instance_id = action.gate_instance_id},
                request);
        }

        (void)mIpcService->UnbindLocalPlayer(action.player_id);

        std::scoped_lock lock(mMutex);
        const auto it = mPlayers.find(action.player_id);
        if (it == mPlayers.end())
        {
            continue;
        }
        if (mLeaseService->IsHeldLocally(action.player_id))
        {
            continue;
        }

        if (it->second.state == PlayerSessionState::online)
        {
            it->second.state = PlayerSessionState::detached;
            it->second.last_state_change_ms = now;
            it->second.detach_deadline_ms = now + static_cast<std::uint64_t>(mDetachedTtlSeconds) * 1000;
            it->second.gate_service_type = 0;
            it->second.gate_instance_id = 0;
            it->second.gate_session_id = 0;
            ++reconciled;
            continue;
        }

        if (it->second.state == PlayerSessionState::detached)
        {
            ++reconciled;
        }
    }

    return reconciled;
}

std::optional<PlayerSessionRecord> PlayerSessionService::Snapshot(const std::uint64_t player_id) const
{
    std::scoped_lock lock(mMutex);
    const auto it = mPlayers.find(player_id);
    if (it == mPlayers.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::uint64_t PlayerSessionService::NowMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

void PlayerSessionService::Loop()
{
    while (!mStopping.load())
    {
        (void)ReconcileLeaseLosses();
        std::this_thread::sleep_for(kSessionReconcileInterval);
    }
}
