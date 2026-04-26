#include "relay_first_policy.h"

namespace ipc
{
namespace
{
bool MatchesLabels(const ProcessDescriptor& process, const StringMap& required_labels)
{
    for (const auto& [required_key, required_value] : required_labels)
    {
        bool matched = false;
        for (const auto& [key, value] : process.labels)
        {
            if (key == required_key && value == required_value)
            {
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            return false;
        }
    }
    return true;
}
} // namespace

RoutePlan RelayFirstPolicy::Resolve(const RoutingContext& context) const
{
    if (context.envelope.header.semantic == DeliverySemantic::broadcast)
    {
        if (context.membership == nullptr || context.envelope.header.target_receiver.type != ReceiverType::service)
        {
            return RoutePlan{RoutePlanKind::drop, {}};
        }

        const ServiceType service_type = context.envelope.broadcast_scope.service_type.value_or(
            static_cast<ServiceType>(context.envelope.header.target_receiver.key_hi));
        RoutePlan plan;
        plan.kind = RoutePlanKind::multi_next_hop;
        for (const auto& process : context.membership->FindByService(service_type))
        {
            if (!context.envelope.broadcast_scope.include_local && process.process == context.self)
            {
                continue;
            }
            if (!MatchesLabels(process, context.envelope.broadcast_scope.required_labels))
            {
                continue;
            }

            plan.hops.push_back(RouteHop{process.process, process.process == context.self});
        }

        return plan.hops.empty() ? RoutePlan{RoutePlanKind::drop, {}} : plan;
    }

    if (context.receiver_location.has_value())
    {
        const ReceiverLocation& location = *context.receiver_location;
        switch (location.kind)
        {
        case ReceiverLocationKind::local:
            return RoutePlan{RoutePlanKind::local_delivery, {}};
        case ReceiverLocationKind::single_process:
            if (!location.processes.empty())
            {
                return ResolveSingleTarget(context, location.processes.front());
            }
            break;
        case ReceiverLocationKind::multi_process: {
            RoutePlan plan;
            plan.kind = RoutePlanKind::multi_next_hop;
            for (const ProcessRef& process : location.processes)
            {
                plan.hops.push_back(RouteHop{process, process == context.self});
            }
            if (!plan.hops.empty())
            {
                return plan;
            }
            break;
        }
        case ReceiverLocationKind::unresolved:
            return RoutePlan{RoutePlanKind::drop, {}};
        }
    }

    if (context.envelope.header.target_receiver.type == ReceiverType::process)
    {
        ProcessRef target{
            ProcessId{
                static_cast<ServiceType>(context.envelope.header.target_receiver.key_hi),
                static_cast<InstanceId>(context.envelope.header.target_receiver.key_lo)},
            0};

        if (context.membership != nullptr)
        {
            const auto descriptor = context.membership->Find(target.process_id);
            if (descriptor.has_value())
            {
                target = descriptor->process;
            }
        }

        return ResolveSingleTarget(context, target);
    }

    return RoutePlan{RoutePlanKind::drop, {}};
}

RoutePlan RelayFirstPolicy::ResolveSingleTarget(const RoutingContext& context, const ProcessRef& target) const
{
    if (target == context.self)
    {
        return RoutePlan{RoutePlanKind::local_delivery, {}};
    }

    if (context.links != nullptr && context.links->HasHealthyDirectLink(target))
    {
        return RoutePlan{
            RoutePlanKind::single_next_hop,
            {RouteHop{target, true}}};
    }

    if (context.membership != nullptr)
    {
        const auto relays = context.membership->FindByService(mRelayServiceType);
        for (const ProcessDescriptor& relay : relays)
        {
            if (relay.process == context.self)
            {
                continue;
            }

            return RoutePlan{
                RoutePlanKind::single_next_hop,
                {RouteHop{relay.process, false}}};
        }
    }

    return RoutePlan{RoutePlanKind::unreachable, {}};
}
} // namespace ipc
