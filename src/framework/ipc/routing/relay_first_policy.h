#pragma once

#include "topology_policy.h"

namespace ipc
{
class RelayFirstPolicy final : public ITopologyPolicy
{
public:
    explicit RelayFirstPolicy(ServiceType relay_service_type)
        : mRelayServiceType(relay_service_type)
    {
    }

    RoutePlan Resolve(const RoutingContext& context) const override;

private:
    RoutePlan ResolveSingleTarget(const RoutingContext& context, const ProcessRef& target) const;

    ServiceType mRelayServiceType = 0;
};
} // namespace ipc
