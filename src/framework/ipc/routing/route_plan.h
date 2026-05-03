#pragma once

#include "../base/process.h"

#include <vector>

namespace ipc
{
// High-level outcome produced by routing for one logical send request.
enum class RoutePlanKind : std::uint8_t
{
    local_delivery,
    single_next_hop,
    multi_next_hop,
    unreachable,
    drop
};

// One concrete next-hop decision inside a resolved route plan.
struct RouteHop
{
    ProcessRef next_hop;
    bool direct = false;
};

// Executable routing result consumed by messenger and remote senders.
struct RoutePlan
{
    RoutePlanKind kind = RoutePlanKind::unreachable;
    std::vector<RouteHop> hops;
};
} // namespace ipc
