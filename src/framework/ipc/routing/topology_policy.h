#pragma once

#include "routing_context.h"
#include "route_plan.h"

namespace ipc
{
class ITopologyPolicy
{
public:
    virtual ~ITopologyPolicy() = default;

    virtual RoutePlan Resolve(const RoutingContext& context) const = 0;
};
} // namespace ipc
