#include "framework/ipc/discovery/discovery.h"
#include "framework/ipc/routing/relay_first_policy.h"
#include "framework/ipc/routing/router.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    class EmptyLinkView final : public ipc::ILinkView
    {
    public:
        bool HasHealthyDirectLink(const ipc::ProcessRef&) const override { return false; }
        std::vector<ipc::ProcessRef> GetHealthyLinks() const override { return {}; }
    };

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    ipc::ProcessDescriptor MakeProcess(ipc::ServiceType service_type, ipc::InstanceId instance_id, ipc::IncarnationId incarnation)
    {
        ipc::ProcessDescriptor descriptor;
        descriptor.process = ipc::ProcessRef{ipc::ProcessId{service_type, instance_id}, incarnation};
        descriptor.service_name = service_type == 99 ? "relay" : "game";
        descriptor.listen_endpoint = {"127.0.0.1", static_cast<std::uint16_t>(9000 + instance_id)};
        descriptor.protocol_version = 1;
        descriptor.start_time_unix_ms = 1;
        return descriptor;
    }

    void TestDiscoveryLifecycle()
    {
        ipc::Discovery discovery;
        const ipc::ProcessDescriptor game = MakeProcess(10, 1, 101);

        Require(discovery.RegisterSelf(game).ok, "register self");
        const auto found = discovery.Find(game.process.process_id);
        Require(found.has_value(), "find registered process");
        Require(found->process == game.process, "found process ref");

        const auto events = discovery.DrainEvents();
        Require(events.size() == 1, "added event count");
        Require(events.front().type == ipc::MembershipEventType::added, "added event type");

        Require(discovery.Remove(game.process.process_id).ok, "remove process");
        const auto removed_events = discovery.DrainEvents();
        Require(removed_events.size() == 1, "removed event count");
        Require(removed_events.front().type == ipc::MembershipEventType::removed, "removed event type");
    }

    void TestRelayFirstRoute()
    {
        ipc::Discovery discovery;
        const ipc::ProcessDescriptor relay = MakeProcess(99, 1, 201);
        const ipc::ProcessDescriptor target = MakeProcess(10, 2, 301);
        Require(discovery.Upsert(relay).ok, "upsert relay");
        Require(discovery.Upsert(target).ok, "upsert target");

        ipc::Envelope envelope;
        envelope.header.source_process = ipc::ProcessRef{ipc::ProcessId{10, 1}, 101};
        envelope.header.target_receiver = {ipc::ReceiverType::process, 10, 2};

        EmptyLinkView links;
        ipc::RoutingContext context{
            envelope.header.source_process,
            envelope,
            std::nullopt,
            &discovery,
            &links};

        ipc::RelayFirstPolicy policy(99);
        ipc::Router router(policy);
        const ipc::RoutePlan plan = router.Resolve(context);

        Require(plan.kind == ipc::RoutePlanKind::single_next_hop, "relay route kind");
        Require(plan.hops.size() == 1, "relay route hop count");
        Require(plan.hops.front().next_hop == relay.process, "relay selected");
        Require(!plan.hops.front().direct, "relay path should not be direct");
    }
} // namespace

int main()
{
    try
    {
        TestDiscoveryLifecycle();
        TestRelayFirstRoute();
        std::cout << "ipc_discovery_routing_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "ipc_discovery_routing_test: " << ex.what() << std::endl;
        return 1;
    }
}
