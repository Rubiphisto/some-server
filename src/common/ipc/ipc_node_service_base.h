#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/discovery/etcd_discovery.h"
#include "../../framework/ipc/link/link_manager.h"
#include "../../framework/ipc/messaging/messenger.h"
#include "../../framework/ipc/messaging/payload_registry.h"
#include "../../framework/ipc/messaging/transport_message_sender.h"
#include "../../framework/ipc/receiver/local_receiver_directory.h"
#include "../../framework/ipc/receiver/receiver_registry.h"
#include "../../framework/ipc/routing/relay_first_policy.h"
#include "../../framework/ipc/routing/router.h"
#include "../../framework/ipc/transport/tcp_transport.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include <string_view>

namespace some_server::common
{
namespace ipc = ::ipc;

struct IpcNodeCore
{
    explicit IpcNodeCore(ipc::EtcdDiscoveryOptions options, ipc::ServiceType relay_service_type);

    ipc::RelayFirstPolicy routing_policy;
    ipc::Router router;
    std::unique_ptr<ipc::TcpTransport> transport;
    std::unique_ptr<ipc::LinkManager> link_manager;
    ipc::EtcdDiscovery discovery;
    ipc::LocalReceiverDirectory receiver_directory;
    ipc::ReceiverRegistry receiver_registry;
    ipc::PayloadRegistry payload_registry;
    std::unique_ptr<ipc::TransportMessageSender> transport_message_sender;
    std::unique_ptr<ipc::Messenger> messenger;
    std::optional<ipc::ProcessDescriptor> self;
    bool registered = false;
    bool transport_ready = false;
    bool ipc_ready = false;
    std::string last_error;
    std::mutex mutex;
    std::condition_variable keepalive_wakeup;
    std::thread keepalive_thread;
    bool stop_keepalive = false;
    std::atomic<bool> keepalive_running = false;
    std::uint32_t keepalive_interval_seconds = 0;
    std::uint64_t keepalive_failure_count = 0;
    std::uint64_t discovery_recovery_success_count = 0;
    std::uint64_t discovery_recovery_failure_count = 0;
    std::condition_variable auto_connect_wakeup;
    std::thread auto_connect_thread;
    bool stop_auto_connect = false;
    std::unordered_set<std::uint64_t> auto_connect_attempts;
    std::uint64_t auto_connect_success_count = 0;
    std::uint64_t auto_connect_failure_count = 0;
    std::optional<ipc::ProcessRef> last_auto_connect_target;
    std::optional<ipc::ProcessRef> last_auto_connect_failure_target;
    std::string last_auto_connect_failure_reason;
};

struct IpcNodeBaseStatusSnapshot
{
    ipc::ProcessDescriptor self;
    bool has_self = false;
    bool transport_ready = false;
    bool registered = false;
    bool ipc_ready = false;
    bool membership_degraded = false;
    bool keepalive_running = false;
    bool watch_running = false;
    std::uint64_t keepalive_failure_count = 0;
    std::uint64_t discovery_recovery_success_count = 0;
    std::uint64_t discovery_recovery_failure_count = 0;
    ipc::EtcdDiscoveryRuntimeStats discovery_runtime;
    std::size_t member_count = 0;
    std::size_t auto_connect_targets = 0;
    std::uint64_t auto_connect_success_count = 0;
    std::uint64_t auto_connect_failure_count = 0;
    bool has_last_auto_connect_target = false;
    ipc::ProcessRef last_auto_connect_target;
    bool has_last_auto_connect_failure_target = false;
    ipc::ProcessRef last_auto_connect_failure_target;
    std::string last_auto_connect_failure_reason;
    std::string last_error;
};

class IpcNodeServiceBase : public ServiceBase
{
public:
    IpcNodeServiceBase(
        std::string name,
        std::int32_t batch,
        ipc::EtcdDiscoveryOptions discovery_options,
        ipc::ServiceType relay_service_type);

protected:
    LifecycleTask LoadIpcRuntime();
    LifecycleTask StartIpcRuntime();
    LifecycleTask StopIpcRuntime();
    LifecycleTask UnloadIpcRuntime();

    ipc::Result RefreshDiscovery();
    ipc::Result KeepAliveOnce();
    std::vector<ipc::MembershipEvent> DrainMembershipEvents();
    std::vector<ipc::ProcessDescriptor> Members() const;
    std::vector<ipc::ProcessRef> HealthyLinks() const;
    void FlushLinkFrames();
    bool IsIpcActiveLocked() const;
    static std::uint64_t MakeProcessKey(const ipc::ProcessId& id);
    std::optional<ipc::ProcessDescriptor> FindDiscoveredMemberLocked(
        ipc::ServiceType service_type,
        ipc::InstanceId instance_id,
        bool exclude_self = true) const;
    ipc::Result ConnectDiscoveredMemberLocked(const ipc::ProcessDescriptor& member);
    void RecordAutoConnectFailureLocked(
        const ipc::ProcessDescriptor& member,
        const ipc::Result& result,
        std::string_view actor_name);
    void RecordAutoConnectSuccessLocked(const ipc::ProcessDescriptor& member, std::string_view actor_name);
    void StartAutoConnectLoop();
    void StopAutoConnectLoop();
    void ReconcileAutoConnectMembers();

    virtual ipc::ProcessDescriptor BuildSelfDescriptor() const = 0;
    virtual ipc::Result SetupRoleComponentsLocked() = 0;
    virtual void TeardownRoleComponentsLocked() {}
    virtual void HandleIncomingDataFrameLocked(const ipc::RawFrame& frame) = 0;
    virtual void HandleDiscoveryFailureLockedExtra(const std::string& message) {}
    virtual void OnDiscoveryRecovered() {}
    virtual bool ShouldRefreshAutoConnectLocked() const { return false; }
    virtual void TryAutoConnectMember(const ipc::ProcessDescriptor& member) = 0;
    IpcNodeBaseStatusSnapshot SnapshotBaseStatusLocked() const;

    IpcNodeCore mCore;
    ipc::RelayFirstPolicy& mRoutingPolicy;
    ipc::Router& mRouter;
    std::unique_ptr<ipc::TcpTransport>& mTransport;
    std::unique_ptr<ipc::LinkManager>& mLinkManager;
    ipc::EtcdDiscovery& mDiscovery;
    ipc::LocalReceiverDirectory& mReceiverDirectory;
    ipc::ReceiverRegistry& mReceiverRegistry;
    ipc::PayloadRegistry& mPayloadRegistry;
    std::unique_ptr<ipc::TransportMessageSender>& mTransportMessageSender;
    std::unique_ptr<ipc::Messenger>& mMessenger;
    std::optional<ipc::ProcessDescriptor>& mSelf;
    bool& mRegistered;
    bool& mTransportReady;
    bool& mIpcReady;
    std::string& mLastError;
    std::mutex& mMutex;
    std::condition_variable& mKeepAliveWakeup;
    std::thread& mKeepAliveThread;
    bool& mStopKeepAlive;
    std::atomic<bool>& mKeepAliveRunning;
    std::uint64_t& mKeepAliveFailureCount;
    std::uint64_t& mDiscoveryRecoverySuccessCount;
    std::uint64_t& mDiscoveryRecoveryFailureCount;
    std::condition_variable& mAutoConnectWakeup;
    std::thread& mAutoConnectThread;
    bool& mStopAutoConnect;
    std::unordered_set<std::uint64_t>& mAutoConnectAttempts;
    std::uint64_t& mAutoConnectSuccessCount;
    std::uint64_t& mAutoConnectFailureCount;
    std::optional<ipc::ProcessRef>& mLastAutoConnectTarget;
    std::optional<ipc::ProcessRef>& mLastAutoConnectFailureTarget;
    std::string& mLastAutoConnectFailureReason;

private:
    void StartKeepAliveLoop();
    void StopKeepAliveLoop();
    void KeepAliveLoop(std::uint32_t interval_seconds);
    void AutoConnectLoop();
    void HandleMembershipEvent(const ipc::MembershipEvent& event);
    ipc::Result TryRecoverDiscovery();
    void HandleDiscoveryFailureLocked(const std::string& message);
};
} // namespace some_server::common
