#pragma once

#include "topology_policy.h"

namespace ipc
{
// Thin facade around the active topology policy used by messenger.
class Router
{
public:
    // Binds router to the topology policy chosen by the application.
    explicit Router(const ITopologyPolicy& policy)
        : mPolicy(policy)
    {
    }

    // Resolves one routing context into the concrete next-hop plan to execute.
    RoutePlan Resolve(const RoutingContext& context) const;

private:
    const ITopologyPolicy& mPolicy;
};
} // namespace ipc
