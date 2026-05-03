#pragma once

#include "../base/result.h"
#include "etcd_discovery_backend.h"
#include "membership_view.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ipc
{
// Runtime counters exported for discovery diagnostics and metrics commands.
struct EtcdDiscoveryRuntimeStats
{
    std::uint64_t watch_restart_count = 0;
    std::uint64_t watch_start_failure_count = 0;
    std::uint64_t watch_stream_closed_count = 0;
    std::uint64_t snapshot_refresh_failure_count = 0;
};

// Production discovery coordinator built on top of an etcd backend contract.
class EtcdDiscovery final : public IMembershipView
{
public:
    // Builds discovery around the default SDK backend using the supplied options.
    explicit EtcdDiscovery(EtcdDiscoveryOptions options);
    // Builds discovery around an injected backend, mainly for tests and fakes.
    EtcdDiscovery(EtcdDiscoveryOptions options, std::unique_ptr<IEtcdDiscoveryBackend> backend);
    ~EtcdDiscovery();

    // Registers or refreshes the local process record in the backend.
    Result RegisterSelf(const ProcessDescriptor& self);
    // Sends one bounded lease keepalive for the current self registration.
    Result KeepAliveOnce();
    // Pulls a fresh backend snapshot and updates local membership state.
    Result RefreshSnapshot();
    // Starts the background watch/recovery loop for member changes.
    Result StartWatch();
    // Stops the background watch loop and any active backend watch stream.
    void StopWatch();
    // Reports whether the discovery watch loop is currently active.
    bool WatchRunning() const;
    // Returns runtime counters collected by the watch and snapshot paths.
    EtcdDiscoveryRuntimeStats RuntimeStats() const;
    // Removes one process record from the backend and local snapshot.
    Result Remove(const ProcessId& id);
    // Drains accumulated membership events since the last call.
    std::vector<MembershipEvent> DrainEvents();

    // Finds one process in the current discovered membership snapshot.
    std::optional<ProcessDescriptor> Find(const ProcessId& id) const override;
    // Returns all discovered members of the requested service type.
    std::vector<ProcessDescriptor> FindByService(ServiceType type) const override;
    // Returns the full current membership snapshot.
    std::vector<ProcessDescriptor> All() const override;

private:
    static std::uint64_t MakeKey(const ProcessId& id);
    static std::string SerializeDescriptor(const ProcessDescriptor& process);
    static Result DeserializeDescriptor(const std::string& json, ProcessDescriptor& process);
    static Result ParseSnapshot(
        const std::string& output,
        std::unordered_map<std::uint64_t, ProcessDescriptor>& refreshed);
    Result GrantLease();
    void ApplySnapshot(std::unordered_map<std::uint64_t, ProcessDescriptor> refreshed);
    void WatchLoop();

    std::string MemberKey(const ProcessId& id) const;

    EtcdDiscoveryOptions mOptions;
    std::unique_ptr<IEtcdDiscoveryBackend> mBackend;
    std::unordered_map<std::uint64_t, ProcessDescriptor> mProcesses;
    std::vector<MembershipEvent> mEvents;
    std::optional<ProcessDescriptor> mSelf;
    std::uint64_t mLeaseId = 0;
    mutable std::mutex mMutex;
    std::condition_variable mWatchWakeup;
    std::thread mWatchThread;
    bool mStopWatch = false;
    std::atomic<bool> mWatchRunning = false;
    EtcdDiscoveryRuntimeStats mRuntimeStats;
};
} // namespace ipc
