#include "framework/ipc/discovery/discovery.h"
#include "framework/ipc/discovery/etcd_discovery.h"
#include "framework/ipc/discovery/etcd_discovery_backend.h"
#include "framework/ipc/routing/relay_first_policy.h"
#include "framework/ipc/routing/router.h"

#include <glaze/glaze.hpp>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>

namespace
{
    struct FakeSerializableProcessDescriptor
    {
        ipc::ServiceType service_type = 0;
        ipc::InstanceId instance_id = 0;
        ipc::IncarnationId incarnation_id = 0;
        std::string service_name;
        std::string host;
        std::uint16_t port = 0;
        std::uint32_t protocol_version = 0;
        std::uint64_t start_time_unix_ms = 0;
        std::vector<ipc::ServiceType> relay_capabilities;
        std::vector<std::pair<std::string, std::string>> labels;
    };

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

    std::string Base64Encode(const std::string& plain)
    {
        static constexpr char kAlphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string encoded;
        int val = 0;
        int valb = -6;
        for (const unsigned char ch : plain)
        {
            val = (val << 8) + ch;
            valb += 8;
            while (valb >= 0)
            {
                encoded.push_back(kAlphabet[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6)
        {
            encoded.push_back(kAlphabet[((val << 8) >> (valb + 8)) & 0x3F]);
        }
        while (encoded.size() % 4 != 0)
        {
            encoded.push_back('=');
        }
        return encoded;
    }

    std::string SerializeDescriptorJson(const ipc::ProcessDescriptor& process)
    {
        const FakeSerializableProcessDescriptor serializable{
            .service_type = process.process.process_id.service_type,
            .instance_id = process.process.process_id.instance_id,
            .incarnation_id = process.process.incarnation_id,
            .service_name = process.service_name,
            .host = process.listen_endpoint.host,
            .port = process.listen_endpoint.port,
            .protocol_version = process.protocol_version,
            .start_time_unix_ms = process.start_time_unix_ms,
            .relay_capabilities = process.relay_capabilities,
            .labels = process.labels,
        };

        auto json = glz::write_json(serializable);
        Require(json.has_value(), "serialize fake descriptor json");
        return *json;
    }

    std::string SnapshotJson(const std::vector<ipc::ProcessDescriptor>& processes)
    {
        std::string json = "{\"kvs\":[";
        bool first = true;
        for (const auto& process : processes)
        {
            if (!first)
            {
                json += ",";
            }
            first = false;
            json += "{";
            json += "\"key\":\"ignored\",";
            json += "\"value\":\"" + Base64Encode(SerializeDescriptorJson(process)) + "\"";
            json += "}";
        }
        json += "]}";
        return json;
    }

    class FakeEtcdBackend final : public ipc::IEtcdDiscoveryBackend
    {
    public:
        ipc::Result GrantLease(std::uint32_t, std::uint64_t& lease_id) override
        {
            grant_lease_calls++;
            lease_id = granted_lease_id;
            return ipc::Result::Success();
        }

        ipc::Result Put(const std::string& key, const std::string&, std::uint64_t lease_id) override
        {
            put_calls++;
            last_put_key = key;
            last_put_lease_id = lease_id;
            return ipc::Result::Success();
        }

        ipc::Result KeepAliveOnce(std::uint64_t lease_id) override
        {
            keepalive_calls++;
            last_keepalive_lease_id = lease_id;
            return keepalive_result;
        }

        ipc::Result Delete(const std::string& key) override
        {
            delete_calls++;
            last_delete_key = key;
            return ipc::Result::Success();
        }

        ipc::Result GetPrefix(const std::string&, std::string& output) override
        {
            std::scoped_lock lock(mMutex);
            get_prefix_calls++;
            output = snapshot_output;
            return ipc::Result::Success();
        }

        ipc::Result StartWatchPrefix(const std::string&) override
        {
            std::scoped_lock lock(mMutex);
            watch_start_calls++;
            if (!start_watch_results.empty())
            {
                const ipc::Result result = start_watch_results.front();
                start_watch_results.erase(start_watch_results.begin());
                if (!result.ok)
                {
                    running = false;
                    return result;
                }
            }
            stopped = false;
            running = true;
            return ipc::Result::Success();
        }

        ipc::WatchPollResult WaitForWatchEvent() override
        {
            std::unique_lock lock(mMutex);
            mCv.wait(lock, [this] { return stopped || !watch_results.empty(); });
            if (stopped)
            {
                return ipc::WatchPollResult{ipc::WatchPollKind::stopped, {}};
            }
            const auto result = watch_results.front();
            watch_results.erase(watch_results.begin());
            return result;
        }

        void StopWatch() override
        {
            {
                std::scoped_lock lock(mMutex);
                stop_watch_calls++;
                stopped = true;
                running = false;
            }
            mCv.notify_all();
        }

        bool WatchRunning() const override
        {
            return running;
        }

        void SetSnapshot(const std::vector<ipc::ProcessDescriptor>& processes)
        {
            std::scoped_lock lock(mMutex);
            snapshot_output = SnapshotJson(processes);
        }

        void PushWatchResult(ipc::WatchPollKind kind)
        {
            {
                std::scoped_lock lock(mMutex);
                watch_results.push_back(ipc::WatchPollResult{kind, {}});
            }
            mCv.notify_all();
        }

        std::uint64_t granted_lease_id = 0xabc;
        std::uint64_t last_put_lease_id = 0;
        std::uint64_t last_keepalive_lease_id = 0;
        ipc::Result keepalive_result = ipc::Result::Success();
        int grant_lease_calls = 0;
        int put_calls = 0;
        int keepalive_calls = 0;
        int delete_calls = 0;
        int get_prefix_calls = 0;
        int watch_start_calls = 0;
        int stop_watch_calls = 0;
        std::string last_put_key;
        std::string last_delete_key;
        std::vector<ipc::Result> start_watch_results;

    private:
        mutable std::mutex mMutex;
        std::condition_variable mCv;
        std::string snapshot_output = SnapshotJson({});
        std::vector<ipc::WatchPollResult> watch_results;
        bool stopped = false;
        bool running = false;
    };

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

    void TestEtcdDiscoveryBackendDelegation()
    {
        auto backend = std::make_unique<FakeEtcdBackend>();
        auto* backend_ptr = backend.get();
        ipc::EtcdDiscovery discovery(
            {.prefix = "/some_server/ipc/test/backend", .lease_ttl_seconds = 5},
            std::move(backend));

        const ipc::ProcessDescriptor game = MakeProcess(10, 7, 701);
        Require(discovery.RegisterSelf(game).ok, "backend register self");
        Require(backend_ptr->grant_lease_calls == 1, "grant lease called");
        Require(backend_ptr->put_calls == 1, "put called");
        Require(backend_ptr->last_put_lease_id == backend_ptr->granted_lease_id, "put uses granted lease");

        Require(discovery.KeepAliveOnce().ok, "backend keepalive");
        Require(backend_ptr->keepalive_calls == 1, "keepalive called");
        Require(
            backend_ptr->last_keepalive_lease_id == backend_ptr->granted_lease_id,
            "keepalive uses granted lease");

        Require(discovery.Remove(game.process.process_id).ok, "backend remove");
        Require(backend_ptr->delete_calls == 1, "delete called");
    }

    void TestEtcdDiscoveryWatchRecoveryWithFakeBackend()
    {
        auto backend = std::make_unique<FakeEtcdBackend>();
        auto* backend_ptr = backend.get();
        ipc::EtcdDiscovery discovery(
            {.prefix = "/some_server/ipc/test/watch_fake", .lease_ttl_seconds = 0},
            std::move(backend));

        const ipc::ProcessDescriptor game = MakeProcess(10, 8, 801);

        backend_ptr->SetSnapshot({});
        Require(discovery.StartWatch().ok, "start fake backend watch");
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            if (backend_ptr->watch_start_calls >= 1)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        Require(backend_ptr->watch_start_calls >= 1, "watch started");

        backend_ptr->SetSnapshot({game});
        backend_ptr->PushWatchResult(ipc::WatchPollKind::event);
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            const auto found = discovery.Find(game.process.process_id);
            if (found.has_value() && found->process == game.process)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        const auto found = discovery.Find(game.process.process_id);
        Require(found.has_value(), "watch event updates snapshot");

        auto events = discovery.DrainEvents();
        Require(!events.empty(), "watch add event exists");
        Require(events.back().type == ipc::MembershipEventType::added, "watch add event type");

        backend_ptr->SetSnapshot({});
        backend_ptr->PushWatchResult(ipc::WatchPollKind::stream_closed);
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            if (!discovery.Find(game.process.process_id).has_value())
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        Require(!discovery.Find(game.process.process_id).has_value(), "stream close triggers snapshot recovery");
        events = discovery.DrainEvents();
        Require(!events.empty(), "watch remove event exists");
        Require(events.back().type == ipc::MembershipEventType::removed, "watch remove event type");
        discovery.StopWatch();
    }

    void TestEtcdDiscoveryWatchStartRetryWithFakeBackend()
    {
        auto backend = std::make_unique<FakeEtcdBackend>();
        auto* backend_ptr = backend.get();
        backend_ptr->start_watch_results.push_back(ipc::Result::Failure("transient watch start failure"));
        backend_ptr->start_watch_results.push_back(ipc::Result::Success());

        ipc::EtcdDiscovery discovery(
            {.prefix = "/some_server/ipc/test/watch_retry", .lease_ttl_seconds = 0},
            std::move(backend));

        Require(discovery.StartWatch().ok, "start retry watch thread");
        for (int attempt = 0; attempt < 30; ++attempt)
        {
            if (backend_ptr->watch_start_calls >= 2 && discovery.WatchRunning())
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        Require(backend_ptr->watch_start_calls >= 2, "watch start retried");
        Require(discovery.WatchRunning(), "watch running after retry");
        discovery.StopWatch();
    }

    void TestEtcdDiscoveryStopWatchWithFakeBackend()
    {
        auto backend = std::make_unique<FakeEtcdBackend>();
        auto* backend_ptr = backend.get();
        ipc::EtcdDiscovery discovery(
            {.prefix = "/some_server/ipc/test/watch_stop", .lease_ttl_seconds = 0},
            std::move(backend));

        Require(discovery.StartWatch().ok, "start watch for stop");
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            if (backend_ptr->watch_start_calls >= 1)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        discovery.StopWatch();
        Require(!discovery.WatchRunning(), "watch stopped");
        Require(backend_ptr->stop_watch_calls >= 1, "backend stop called");
    }
} // namespace

template <>
struct glz::meta<FakeSerializableProcessDescriptor>
{
    using T = FakeSerializableProcessDescriptor;
    static constexpr auto value = glz::object(
        "service_type",
        &T::service_type,
        "instance_id",
        &T::instance_id,
        "incarnation_id",
        &T::incarnation_id,
        "service_name",
        &T::service_name,
        "host",
        &T::host,
        "port",
        &T::port,
        "protocol_version",
        &T::protocol_version,
        "start_time_unix_ms",
        &T::start_time_unix_ms,
        "relay_capabilities",
        &T::relay_capabilities,
        "labels",
        &T::labels);
};

int main()
{
    try
    {
        TestDiscoveryLifecycle();
        TestRelayFirstRoute();
        TestEtcdDiscoveryBackendDelegation();
        TestEtcdDiscoveryWatchRecoveryWithFakeBackend();
        TestEtcdDiscoveryWatchStartRetryWithFakeBackend();
        TestEtcdDiscoveryStopWatchWithFakeBackend();
        std::cout << "ipc_discovery_routing_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "ipc_discovery_routing_test: " << ex.what() << std::endl;
        return 1;
    }
}
