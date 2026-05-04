#pragma once

#include "../application.h"
#include "../../common/ipc/ipc_node_service_base.h"

#include <atomic>
#include <google/protobuf/wrappers.pb.h>
#include <mutex>
#include <optional>
#include <string>

struct RelayIpcStatus
{
    ipc::ProcessDescriptor self;
    bool transport_ready = false;
    bool registered = false;
    bool ipc_ready = false;
    bool membership_degraded = false;
    bool keepalive_running = false;
    bool watch_running = false;
    std::uint64_t keepalive_failure_count = 0;
    std::uint64_t discovery_recovery_success_count = 0;
    std::uint64_t discovery_recovery_failure_count = 0;
    std::uint64_t forward_failure_count = 0;
    std::string last_forward_failure_reason;
    ipc::EtcdDiscoveryRuntimeStats discovery_runtime;
    std::size_t member_count = 0;
    std::size_t visible_game_members = 0;
    std::size_t healthy_game_links = 0;
    std::size_t auto_connect_targets = 0;
    std::uint64_t auto_connect_success_count = 0;
    std::uint64_t auto_connect_failure_count = 0;
    bool has_last_auto_connect_target = false;
    ipc::ProcessRef last_auto_connect_target;
    bool has_last_auto_connect_failure_target = false;
    ipc::ProcessRef last_auto_connect_failure_target;
    std::string last_auto_connect_failure_reason;
    std::uint64_t forwarded_data_frame_count = 0;
    std::string last_error;
};

class RelayIpcService final : public some_server::common::IpcNodeServiceBase
{
public:
    RelayIpcService(const RelayConfiguration& configuration, ipc::ServiceType relay_service_type);

    LifecycleTask Load() override;
    LifecycleTask Start() override;
    LifecycleTask Stop() override;
    LifecycleTask Unload() override;

    using IpcNodeServiceBase::DrainMembershipEvents;
    using IpcNodeServiceBase::HealthyLinks;
    using IpcNodeServiceBase::KeepAliveOnce;
    using IpcNodeServiceBase::Members;
    using IpcNodeServiceBase::RefreshDiscovery;

    RelayIpcStatus Snapshot() const;
    ipc::Result ConnectToMember(ipc::ServiceType service_type, ipc::InstanceId instance_id);

private:
    ipc::ProcessDescriptor BuildSelfDescriptor() const override;
    ipc::Result SetupRoleComponentsLocked() override;
    void HandleIncomingDataFrameLocked(const ipc::RawFrame& frame) override;
    void HandleDiscoveryFailureLockedExtra(const std::string& message) override;
    void OnDiscoveryRecovered() override;
    void TryAutoConnectMember(const ipc::ProcessDescriptor& member) override;

    void RecordForwardFailureLocked(const std::string& reason);

    RelayConfiguration mConfiguration;
    ipc::ServiceType mRelayServiceType = 0;
    std::atomic<std::uint64_t> mForwardedDataFrameCount = 0;
    std::uint64_t mForwardFailureCount = 0;
    std::string mLastForwardFailureReason;
};
