#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/result.h"
#include "player_repository.h"

#include <cstdint>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

class GameIpcClientService;
class PlayerLeaseService;
class PlayerRuntimeService;

enum class PlayerSessionState
{
    loading,
    online,
    detached,
    unloading,
};

const char* ToString(PlayerSessionState state);

struct PlayerSessionRecord
{
    PlayerSessionState state = PlayerSessionState::loading;
    std::uint32_t gate_service_type = 0;
    std::uint32_t gate_instance_id = 0;
    std::uint64_t gate_session_id = 0;
    std::uint64_t activate_count = 0;
    std::uint64_t last_state_change_ms = 0;
    std::uint64_t detach_deadline_ms = 0;
};

class PlayerSessionService final : public ServiceBase
{
public:
    PlayerSessionService(
        PlayerRepository* repository,
        PlayerLeaseService* lease_service,
        PlayerRuntimeService* runtime_service,
        GameIpcClientService* ipc_service,
        std::uint32_t detached_ttl_seconds)
        : ServiceBase("player_session", 30)
        , mRepository(repository)
        , mLeaseService(lease_service)
        , mRuntimeService(runtime_service)
        , mIpcService(ipc_service)
        , mDetachedTtlSeconds(detached_ttl_seconds)
    {
    }

    LifecycleTask Start() override;
    LifecycleTask Stop() override;

    ipc::Result ActivatePlayer(
        std::uint64_t player_id,
        std::uint32_t gate_service_type,
        std::uint32_t gate_instance_id,
        std::uint64_t gate_session_id,
        bool allow_create_without_maria = false);
    ipc::Result DetachPlayer(std::uint64_t player_id);
    ipc::Result DetachPlayer(
        std::uint64_t player_id,
        std::uint32_t gate_service_type,
        std::uint32_t gate_instance_id,
        std::uint64_t gate_session_id);
    ipc::Result ReleasePlayer(std::uint64_t player_id);
    std::size_t ExpireDetachedPlayers();
    std::size_t ReconcileLeaseLosses();
    std::optional<PlayerSessionRecord> Snapshot(std::uint64_t player_id) const;

private:
    static std::uint64_t NowMs();
    void Loop();

    PlayerRepository* mRepository = nullptr;
    PlayerLeaseService* mLeaseService = nullptr;
    PlayerRuntimeService* mRuntimeService = nullptr;
    GameIpcClientService* mIpcService = nullptr;
    std::uint32_t mDetachedTtlSeconds = 0;
    std::atomic<bool> mStopping = false;
    std::thread mThread;
    mutable std::mutex mMutex;
    std::unordered_map<std::uint64_t, PlayerSessionRecord> mPlayers;
};
