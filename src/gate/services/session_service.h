#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/result.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

enum class GateSessionState
{
    anonymous,
    logging_in,
    active,
    kicked,
    reconnecting,
    closing,
};

const char* ToString(GateSessionState state);

struct GateSessionRecord
{
    GateSessionState state = GateSessionState::anonymous;
    std::uint64_t gate_session_id = 0;
    std::uint64_t connection_id = 0;
    std::string account_id;
    std::uint64_t player_id = 0;
    std::uint32_t game_service_type = 0;
    std::uint32_t game_instance_id = 0;
    std::uint64_t login_version = 0;
    std::uint64_t session_epoch = 0;
    std::uint64_t last_active_time_ms = 0;
};

class GateSessionService final : public ServiceBase
{
public:
    GateSessionService()
        : ServiceBase("gate_session", 20)
    {
    }

    ipc::Result OpenAnonymous(std::uint64_t gate_session_id, std::uint64_t connection_id);
    ipc::Result Activate(
        std::uint64_t gate_session_id,
        std::string_view account_id,
        std::uint64_t player_id,
        std::uint32_t game_service_type,
        std::uint32_t game_instance_id);
    std::optional<GateSessionRecord> FindByAccount(std::string_view account_id) const;
    std::optional<GateSessionRecord> FindByConnection(std::uint64_t connection_id) const;
    ipc::Result RemoveByConnection(std::uint64_t connection_id, GateSessionRecord* removed = nullptr);
    std::optional<GateSessionRecord> Snapshot(std::uint64_t gate_session_id) const;

private:
    static std::uint64_t NowMs();

    mutable std::mutex mMutex;
    std::unordered_map<std::uint64_t, GateSessionRecord> mSessions;
};
