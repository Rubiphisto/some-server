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
    // Builds the shared IPC runtime state used by node-level IPC services.
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
    // Creates a shared IPC service base with discovery options and first-phase relay policy.
    IpcNodeServiceBase(
        std::string name,
        std::int32_t batch,
        ipc::EtcdDiscoveryOptions discovery_options,
        ipc::ServiceType relay_service_type);

protected:
    // Initializes the shared IPC runtime objects and role-specific local components.
    LifecycleTask LoadIpcRuntime();
    // Starts listening, registers discovery, refreshes membership and starts watch/keepalive.
    LifecycleTask StartIpcRuntime();
    // Stops auto-connect and keepalive loops, stops watch and removes this node from discovery.
    LifecycleTask StopIpcRuntime();
    // Releases the shared IPC runtime and clears role-local registries/state.
    LifecycleTask UnloadIpcRuntime();

    // Refreshes the discovery snapshot from etcd and updates the last error on failure.
    ipc::Result RefreshDiscovery();
    // Performs one bounded keepalive attempt for the current lease.
    ipc::Result KeepAliveOnce();
    // Drains membership events accumulated by discovery since the last poll.
    std::vector<ipc::MembershipEvent> DrainMembershipEvents();
    // Returns the current known discovery members.
    std::vector<ipc::ProcessDescriptor> Members() const;
    // Returns the currently healthy process links tracked by the link manager.
    std::vector<ipc::ProcessRef> HealthyLinks() const;
    // Flushes control/data frames produced by the link layer to the transport.
    void FlushLinkFrames();
    // Returns whether this node should still participate in IPC under the current runtime state.
    bool IsIpcActiveLocked() const;
    // Builds a stable integer key for process-level auto-connect bookkeeping.
    static std::uint64_t MakeProcessKey(const ipc::ProcessId& id);
    // Finds a discovered member by service and instance id, optionally excluding self.
    std::optional<ipc::ProcessDescriptor> FindDiscoveredMemberLocked(
        ipc::ServiceType service_type,
        ipc::InstanceId instance_id,
        bool exclude_self = true) const;
    // Attempts to establish a transport/link connection to a discovered member.
    ipc::Result ConnectDiscoveredMemberLocked(const ipc::ProcessDescriptor& member);
    // Records a failed auto-connect attempt and updates common diagnostics fields.
    void RecordAutoConnectFailureLocked(
        const ipc::ProcessDescriptor& member,
        const ipc::Result& result,
        std::string_view actor_name);
    // Records a successful auto-connect attempt and updates common diagnostics fields.
    void RecordAutoConnectSuccessLocked(const ipc::ProcessDescriptor& member, std::string_view actor_name);
    // Starts the background loop that reconciles membership and performs role-specific auto-connect.
    void StartAutoConnectLoop();
    // Stops the background auto-connect loop and waits for it to exit.
    void StopAutoConnectLoop();
    // Runs one immediate auto-connect reconcile pass over the current membership snapshot.
    void ReconcileAutoConnectMembers();

    // Builds this node's process descriptor before runtime startup.
    virtual ipc::ProcessDescriptor BuildSelfDescriptor() const = 0;
    // Sets up role-specific receivers, payload handlers and other local IPC state.
    virtual ipc::Result SetupRoleComponentsLocked() = 0;
    // Tears down role-specific local IPC state during runtime unload.
    virtual void TeardownRoleComponentsLocked() {}
    // Handles an inbound data frame after common transport/link processing has completed.
    virtual void HandleIncomingDataFrameLocked(const ipc::RawFrame& frame) = 0;
    // Lets the role react to discovery failure after the common degraded handling runs.
    virtual void HandleDiscoveryFailureLockedExtra(const std::string& message) {}
    // Notifies the role after discovery has been successfully recovered.
    virtual void OnDiscoveryRecovered() {}
    // Returns whether the role needs an explicit refresh before the next auto-connect reconcile.
    virtual bool ShouldRefreshAutoConnectLocked() const { return false; }
    // Performs one role-specific auto-connect attempt for a discovered member.
    virtual void TryAutoConnectMember(const ipc::ProcessDescriptor& member) = 0;
    // Builds the common status snapshot shared by concrete IPC node services.
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
