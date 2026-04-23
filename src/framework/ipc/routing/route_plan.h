#pragma once

#include "../base/process.h"

#include <vector>

namespace ipc
{
enum class RoutePlanKind : std::uint8_t
{
    local_delivery,
    single_next_hop,
    multi_next_hop,
    unreachable,
    drop
};

struct RouteHop
{
    ProcessRef next_hop;
    bool direct = false;
};

struct RoutePlan
{
    RoutePlanKind kind = RoutePlanKind::unreachable;
    std::vector<RouteHop> hops;
};
} // namespace ipc
