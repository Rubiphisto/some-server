#include "routing_service.h"

#include <chrono>

ipc::Result GateRoutingService::BindPlayer(
    const std::uint64_t player_id,
    const std::uint32_t game_service_type,
    const std::uint32_t game_instance_id,
    const std::uint64_t gate_session_id)
{
    std::scoped_lock lock(mMutex);
    auto& route = mRoutes[player_id];
    route.game_service_type = game_service_type;
    route.game_instance_id = game_instance_id;
    route.gate_session_id = gate_session_id;
    route.last_update_ms = NowMs();
    return ipc::Result::Success();
}

ipc::Result GateRoutingService::UnbindPlayer(const std::uint64_t player_id, const std::uint64_t gate_session_id)
{
    std::scoped_lock lock(mMutex);
    const auto it = mRoutes.find(player_id);
    if (it == mRoutes.end())
    {
        return ipc::Result::Failure("player route does not exist");
    }
    if (it->second.gate_session_id != gate_session_id)
    {
        return ipc::Result::Failure("player route gate_session_id mismatch");
    }
    mRoutes.erase(it);
    return ipc::Result::Success();
}

std::optional<GatePlayerRoute> GateRoutingService::Snapshot(const std::uint64_t player_id) const
{
    std::scoped_lock lock(mMutex);
    const auto it = mRoutes.find(player_id);
    if (it == mRoutes.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::uint64_t GateRoutingService::NowMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}
