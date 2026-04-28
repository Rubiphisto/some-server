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
class EtcdDiscovery final : public IMembershipView
{
public:
    explicit EtcdDiscovery(EtcdDiscoveryOptions options);
    EtcdDiscovery(EtcdDiscoveryOptions options, std::unique_ptr<IEtcdDiscoveryBackend> backend);
    ~EtcdDiscovery();

    Result RegisterSelf(const ProcessDescriptor& self);
    Result KeepAliveOnce();
    Result RefreshSnapshot();
    Result StartWatch();
    void StopWatch();
    bool WatchRunning() const;
    Result Remove(const ProcessId& id);
    std::vector<MembershipEvent> DrainEvents();

    std::optional<ProcessDescriptor> Find(const ProcessId& id) const override;
    std::vector<ProcessDescriptor> FindByService(ServiceType type) const override;
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
};
} // namespace ipc
