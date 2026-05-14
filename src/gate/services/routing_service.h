#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/result.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>

struct GatePlayerRoute
{
    std::uint32_t game_service_type = 0;
    std::uint32_t game_instance_id = 0;
    std::uint64_t gate_session_id = 0;
    std::uint64_t last_update_ms = 0;
};

class GateRoutingService final : public ServiceBase
{
public:
    GateRoutingService()
        : ServiceBase("gate_routing", 30)
    {
    }

    ipc::Result BindPlayer(
        std::uint64_t player_id,
        std::uint32_t game_service_type,
        std::uint32_t game_instance_id,
        std::uint64_t gate_session_id);
    ipc::Result UnbindPlayer(std::uint64_t player_id, std::uint64_t gate_session_id);
    std::optional<GatePlayerRoute> Snapshot(std::uint64_t player_id) const;

private:
    static std::uint64_t NowMs();

    mutable std::mutex mMutex;
    std::unordered_map<std::uint64_t, GatePlayerRoute> mRoutes;
};
