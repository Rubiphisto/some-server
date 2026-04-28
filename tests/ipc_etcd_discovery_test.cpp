#include "framework/ipc/discovery/etcd_discovery.h"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace
{
    struct ScopedEtcd
    {
        pid_t pid = -1;
        std::filesystem::path data_dir;
        std::string client_endpoint;
        std::uint16_t client_port = 0;
        std::uint16_t peer_port = 0;

        ~ScopedEtcd()
        {
            if (pid > 0)
            {
                kill(pid, SIGTERM);
                waitpid(pid, nullptr, 0);
            }
            std::error_code ignored;
            if (!data_dir.empty())
            {
                std::filesystem::remove_all(data_dir, ignored);
            }
        }
    };

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template <typename Predicate>
    void WaitUntil(Predicate&& predicate, const std::string& message)
    {
        for (int attempt = 0; attempt < 30; ++attempt)
        {
            if (predicate())
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        throw std::runtime_error(message);
    }

    ipc::ProcessDescriptor MakeProcess(ipc::ServiceType service_type, ipc::InstanceId instance_id, ipc::IncarnationId incarnation)
    {
        ipc::ProcessDescriptor descriptor;
        descriptor.process = ipc::ProcessRef{ipc::ProcessId{service_type, instance_id}, incarnation};
        descriptor.service_name = service_type == 99 ? "relay" : "game";
        descriptor.listen_endpoint = {"127.0.0.1", static_cast<std::uint16_t>(9200 + instance_id)};
        descriptor.protocol_version = 1;
        descriptor.start_time_unix_ms = 1;
        descriptor.labels.push_back({"role", descriptor.service_name});
        return descriptor;
    }

    ScopedEtcd StartEtcd()
    {
        ScopedEtcd scoped;
        const auto suffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        scoped.data_dir = std::filesystem::temp_directory_path() / ("ipc_etcd_" + suffix);
        std::filesystem::create_directories(scoped.data_dir);

        const auto seed = static_cast<std::uint16_t>(
            std::chrono::steady_clock::now().time_since_epoch().count() % 1000);
        scoped.client_port = static_cast<std::uint16_t>(32379 + seed * 2);
        scoped.peer_port = static_cast<std::uint16_t>(scoped.client_port + 1);
        const std::string client_url = "http://127.0.0.1:" + std::to_string(scoped.client_port);
        const std::string peer_url = "http://127.0.0.1:" + std::to_string(scoped.peer_port);
        scoped.client_endpoint = "127.0.0.1:" + std::to_string(scoped.client_port);

        const pid_t pid = fork();
        if (pid == -1)
        {
            throw std::runtime_error("fork failed");
        }
        if (pid == 0)
        {
            execl(
                "/usr/local/bin/etcd",
                "etcd",
                "--name",
                "ipc-test",
                "--data-dir",
                scoped.data_dir.c_str(),
                "--listen-client-urls",
                client_url.c_str(),
                "--advertise-client-urls",
                client_url.c_str(),
                "--listen-peer-urls",
                peer_url.c_str(),
                "--initial-advertise-peer-urls",
                peer_url.c_str(),
                "--initial-cluster",
                ("ipc-test=" + peer_url).c_str(),
                "--initial-cluster-state",
                "new",
                static_cast<char*>(nullptr));
            _exit(127);
        }

        scoped.pid = pid;

        for (int attempt = 0; attempt < 50; ++attempt)
        {
            const std::string health_command =
                "etcdctl --endpoints=127.0.0.1:" + std::to_string(scoped.client_port) +
                " endpoint health >/dev/null 2>&1";
            const int rc = std::system(health_command.c_str());
            if (rc == 0)
            {
                return scoped;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        throw std::runtime_error("etcd did not become healthy");
    }

    void TestEtcdDiscoveryLifecycle()
    {
        const ScopedEtcd etcd = StartEtcd();

        ipc::EtcdDiscovery discovery({
            .etcdctl_path = "etcdctl",
            .endpoints = {etcd.client_endpoint},
            .prefix = "/some_server/ipc/test/local"});

        const ipc::ProcessDescriptor game = MakeProcess(10, 1, 101);
        Require(discovery.RegisterSelf(game).ok, "register self");
        Require(discovery.RefreshSnapshot().ok, "refresh snapshot");

        const auto found = discovery.Find(game.process.process_id);
        Require(found.has_value(), "find registered process");
        Require(found->process == game.process, "registered process ref");

        Require(discovery.Remove(game.process.process_id).ok, "remove process");
        Require(discovery.RefreshSnapshot().ok, "refresh after remove");
        Require(!discovery.Find(game.process.process_id).has_value(), "removed process should disappear");
    }

    void TestEtcdDiscoveryLeaseExpiry()
    {
        const ScopedEtcd etcd = StartEtcd();

        ipc::EtcdDiscovery discovery({
            .etcdctl_path = "etcdctl",
            .endpoints = {etcd.client_endpoint},
            .prefix = "/some_server/ipc/test/lease_expiry",
            .lease_ttl_seconds = 2});

        const ipc::ProcessDescriptor game = MakeProcess(10, 2, 102);
        Require(discovery.RegisterSelf(game).ok, "register leased process");
        std::this_thread::sleep_for(std::chrono::seconds(3));
        Require(discovery.RefreshSnapshot().ok, "refresh after lease expiry");
        Require(!discovery.Find(game.process.process_id).has_value(), "expired lease should remove member");

        const auto events = discovery.DrainEvents();
        Require(events.size() == 1, "lease expiry removed event count");
        Require(events.front().type == ipc::MembershipEventType::removed, "lease expiry removed event type");
    }

    void TestEtcdDiscoveryKeepAlive()
    {
        const ScopedEtcd etcd = StartEtcd();

        ipc::EtcdDiscovery discovery({
            .etcdctl_path = "etcdctl",
            .endpoints = {etcd.client_endpoint},
            .prefix = "/some_server/ipc/test/keep_alive",
            .lease_ttl_seconds = 2});

        const ipc::ProcessDescriptor game = MakeProcess(10, 3, 103);
        Require(discovery.RegisterSelf(game).ok, "register keepalive process");
        std::this_thread::sleep_for(std::chrono::seconds(1));
        Require(discovery.KeepAliveOnce().ok, "keepalive once");
        std::this_thread::sleep_for(std::chrono::seconds(1));
        Require(discovery.RefreshSnapshot().ok, "refresh after keepalive");

        const auto found = discovery.Find(game.process.process_id);
        Require(found.has_value(), "keepalive should retain member");
    }

    void TestEtcdDiscoverySnapshotEvents()
    {
        const ScopedEtcd etcd = StartEtcd();

        ipc::EtcdDiscovery writer({
            .etcdctl_path = "etcdctl",
            .endpoints = {etcd.client_endpoint},
            .prefix = "/some_server/ipc/test/events",
            .lease_ttl_seconds = 0});
        ipc::EtcdDiscovery observer({
            .etcdctl_path = "etcdctl",
            .endpoints = {etcd.client_endpoint},
            .prefix = "/some_server/ipc/test/events",
            .lease_ttl_seconds = 0});

        const ipc::ProcessDescriptor game = MakeProcess(10, 4, 104);
        Require(writer.RegisterSelf(game).ok, "register event member");
        Require(observer.RefreshSnapshot().ok, "observer initial snapshot");

        auto events = observer.DrainEvents();
        Require(events.size() == 1, "snapshot add event count");
        Require(events.front().type == ipc::MembershipEventType::added, "snapshot add event type");

        Require(writer.Remove(game.process.process_id).ok, "remove event member");
        Require(observer.RefreshSnapshot().ok, "observer refresh after remove");

        events = observer.DrainEvents();
        Require(events.size() == 1, "snapshot remove event count");
        Require(events.front().type == ipc::MembershipEventType::removed, "snapshot remove event type");
    }

    void TestEtcdDiscoveryWatch()
    {
        const ScopedEtcd etcd = StartEtcd();

        ipc::EtcdDiscovery writer({
            .etcdctl_path = "etcdctl",
            .endpoints = {etcd.client_endpoint},
            .prefix = "/some_server/ipc/test/watch",
            .lease_ttl_seconds = 0});
        ipc::EtcdDiscovery observer({
            .etcdctl_path = "etcdctl",
            .endpoints = {etcd.client_endpoint},
            .prefix = "/some_server/ipc/test/watch",
            .lease_ttl_seconds = 0});

        Require(observer.StartWatch().ok, "start discovery watch");
        const ipc::ProcessDescriptor game = MakeProcess(10, 5, 105);
        Require(writer.RegisterSelf(game).ok, "register watch member");

        WaitUntil(
            [&observer, &game] {
                const auto found = observer.Find(game.process.process_id);
                return found.has_value() && found->process == game.process;
            },
            "watch did not observe add");

        auto events = observer.DrainEvents();
        Require(events.size() == 1, "watch add event count");
        Require(events.front().type == ipc::MembershipEventType::added, "watch add event type");

        Require(writer.Remove(game.process.process_id).ok, "remove watch member");
        WaitUntil(
            [&observer, &game] { return !observer.Find(game.process.process_id).has_value(); },
            "watch did not observe remove");

        events = observer.DrainEvents();
        Require(events.size() == 1, "watch remove event count");
        Require(events.front().type == ipc::MembershipEventType::removed, "watch remove event type");
        observer.StopWatch();
    }
} // namespace

int main()
{
    try
    {
        TestEtcdDiscoveryLifecycle();
        TestEtcdDiscoveryLeaseExpiry();
        TestEtcdDiscoveryKeepAlive();
        TestEtcdDiscoverySnapshotEvents();
        TestEtcdDiscoveryWatch();
        std::cout << "ipc_etcd_discovery_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "ipc_etcd_discovery_test: " << ex.what() << std::endl;
        return 1;
    }
}
