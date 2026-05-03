#pragma once

#include "routing_context.h"
#include "route_plan.h"

namespace ipc
{
// Strategy interface that maps one routing context into a concrete route plan.
class ITopologyPolicy
{
public:
    virtual ~ITopologyPolicy() = default;

    // Resolves one send request using the policy's topology rules.
    virtual RoutePlan Resolve(const RoutingContext& context) const = 0;
};
} // namespace ipc
