#include "session_service.h"

#include <chrono>

const char* ToString(const GateSessionState state)
{
    switch (state)
    {
    case GateSessionState::anonymous:
        return "anonymous";
    case GateSessionState::logging_in:
        return "logging_in";
    case GateSessionState::active:
        return "active";
    case GateSessionState::kicked:
        return "kicked";
    case GateSessionState::reconnecting:
        return "reconnecting";
    case GateSessionState::closing:
        return "closing";
    }
    return "unknown";
}

ipc::Result GateSessionService::OpenAnonymous(const std::uint64_t gate_session_id, const std::uint64_t connection_id)
{
    std::scoped_lock lock(mMutex);
    if (mSessions.contains(gate_session_id))
    {
        return ipc::Result::Failure("session already exists");
    }

    auto& record = mSessions[gate_session_id];
    record.state = GateSessionState::anonymous;
    record.gate_session_id = gate_session_id;
    record.connection_id = connection_id;
    record.session_epoch = 1;
    record.last_active_time_ms = NowMs();
    return ipc::Result::Success();
}

ipc::Result GateSessionService::Activate(
    const std::uint64_t gate_session_id,
    const std::string_view account_id,
    const std::uint64_t player_id,
    const std::uint32_t game_service_type,
    const std::uint32_t game_instance_id)
{
    std::scoped_lock lock(mMutex);
    const auto it = mSessions.find(gate_session_id);
    if (it == mSessions.end())
    {
        return ipc::Result::Failure("session does not exist");
    }

    it->second.state = GateSessionState::active;
    it->second.gate_session_id = gate_session_id;
    it->second.account_id = std::string{account_id};
    it->second.player_id = player_id;
    it->second.game_service_type = game_service_type;
    it->second.game_instance_id = game_instance_id;
    ++it->second.login_version;
    ++it->second.session_epoch;
    it->second.last_active_time_ms = NowMs();
    return ipc::Result::Success();
}

std::optional<GateSessionRecord> GateSessionService::FindByConnection(const std::uint64_t connection_id) const
{
    std::scoped_lock lock(mMutex);
    for (const auto& [gate_session_id, record] : mSessions)
    {
        (void)gate_session_id;
        if (record.connection_id == connection_id)
        {
            return record;
        }
    }
    return std::nullopt;
}

std::optional<GateSessionRecord> GateSessionService::FindByAccount(const std::string_view account_id) const
{
    std::scoped_lock lock(mMutex);
    for (const auto& [gate_session_id, record] : mSessions)
    {
        (void)gate_session_id;
        if (record.account_id == account_id &&
            (record.state == GateSessionState::active || record.state == GateSessionState::logging_in))
        {
            return record;
        }
    }
    return std::nullopt;
}

ipc::Result GateSessionService::RemoveByConnection(const std::uint64_t connection_id, GateSessionRecord* removed)
{
    std::scoped_lock lock(mMutex);
    for (auto it = mSessions.begin(); it != mSessions.end(); ++it)
    {
        if (it->second.connection_id == connection_id)
        {
            it->second.state = GateSessionState::closing;
            if (removed != nullptr)
            {
                *removed = it->second;
            }
            mSessions.erase(it);
            return ipc::Result::Success();
        }
    }
    return ipc::Result::Failure("session for connection does not exist");
}

std::optional<GateSessionRecord> GateSessionService::Snapshot(const std::uint64_t gate_session_id) const
{
    std::scoped_lock lock(mMutex);
    const auto it = mSessions.find(gate_session_id);
    if (it == mSessions.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::uint64_t GateSessionService::NowMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}
