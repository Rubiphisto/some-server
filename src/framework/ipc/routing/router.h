#pragma once

#include "topology_policy.h"

namespace ipc
{
class Router
{
public:
    explicit Router(const ITopologyPolicy& policy)
        : mPolicy(policy)
    {
    }

    RoutePlan Resolve(const RoutingContext& context) const;

private:
    const ITopologyPolicy& mPolicy;
};
} // namespace ipc
