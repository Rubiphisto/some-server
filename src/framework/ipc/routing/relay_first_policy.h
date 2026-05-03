#pragma once

#include "topology_policy.h"

namespace ipc
{
// First-phase routing policy that prefers local/direct delivery and otherwise uses relay.
class RelayFirstPolicy final : public ITopologyPolicy
{
public:
    // relay_service_type identifies which discovered service acts as IPC relay.
    explicit RelayFirstPolicy(ServiceType relay_service_type)
        : mRelayServiceType(relay_service_type)
    {
    }

    // Resolves one send request under the first-phase relay-first rules.
    RoutePlan Resolve(const RoutingContext& context) const override;

private:
    RoutePlan ResolveSingleTarget(const RoutingContext& context, const ProcessRef& target) const;

    ServiceType mRelayServiceType = 0;
};
} // namespace ipc
