#pragma once

#include "../base/result.h"
#include "membership_view.h"

#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <sys/types.h>

namespace ipc
{
struct EtcdDiscoveryOptions
{
    std::string etcdctl_path = "etcdctl";
    std::vector<std::string> endpoints{"127.0.0.1:2379"};
    std::string prefix = "/some_server/ipc/dev/local";
    std::uint32_t lease_ttl_seconds = 5;
};

class EtcdDiscovery final : public IMembershipView
{
public:
    explicit EtcdDiscovery(EtcdDiscoveryOptions options);
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
    Result GrantLease();
    Result RefreshSnapshotUnlocked();
    Result StartWatchProcess();
    void StopWatchProcess();
    void WatchLoop();

    std::string EndpointsArg() const;
    std::string MemberKey(const ProcessId& id) const;
    Result RunCommand(const std::string& command, std::string& output) const;
    Result RunPut(const std::string& key, const std::string& value) const;
    Result RunLeaseKeepAliveOnce() const;
    Result RunDelete(const std::string& key) const;
    Result RunGetPrefix(const std::string& key_prefix, std::string& output) const;

    EtcdDiscoveryOptions mOptions;
    std::unordered_map<std::uint64_t, ProcessDescriptor> mProcesses;
    std::vector<MembershipEvent> mEvents;
    std::optional<ProcessDescriptor> mSelf;
    std::uint64_t mLeaseId = 0;
    mutable std::mutex mMutex;
    std::condition_variable mWatchWakeup;
    std::thread mWatchThread;
    bool mStopWatch = false;
    int mWatchPipeFd = -1;
    pid_t mWatchPid = -1;
    std::atomic<bool> mWatchRunning = false;
};
} // namespace ipc
