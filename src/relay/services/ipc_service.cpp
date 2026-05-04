#include "ipc_service.h"

#include "../../common/ipc/first_phase_topology_policy.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace
{
constexpr std::int32_t kRelayIpcBatch = 100;
constexpr ipc::ServiceType kRelayServiceType = 99;
constexpr ipc::ServiceType kGameServiceType = 10;
}

RelayIpcService::RelayIpcService(const RelayConfiguration& configuration, ipc::ServiceType relay_service_type)
    : IpcNodeServiceBase(
          "relay_ipc",
          kRelayIpcBatch,
          ipc::EtcdDiscoveryOptions{
              .endpoints = configuration.discovery.endpoints,
              .prefix = configuration.discovery.prefix,
              .lease_ttl_seconds = configuration.discovery.lease_ttl_seconds},
          kRelayServiceType)
    , mConfiguration(configuration)
    , mRelayServiceType(relay_service_type)
{
}

LifecycleTask RelayIpcService::Load()
{
    return LoadIpcRuntime();
}

LifecycleTask RelayIpcService::Start()
{
    (void)StartIpcRuntime();
    ReconcileAutoConnectMembers();
    StartAutoConnectLoop();

    return LifecycleTask::Completed();
}

LifecycleTask RelayIpcService::Stop()
{
    return StopIpcRuntime();
}

LifecycleTask RelayIpcService::Unload()
{
    return UnloadIpcRuntime();
}

RelayIpcStatus RelayIpcService::Snapshot() const
{
    std::scoped_lock lock(mMutex);
    const auto base = SnapshotBaseStatusLocked();
    RelayIpcStatus status;
    if (base.has_self)
    {
        status.self = base.self;
    }
    status.transport_ready = base.transport_ready;
    status.registered = base.registered;
    status.ipc_ready = base.ipc_ready;
    status.membership_degraded = base.membership_degraded;
    status.keepalive_running = base.keepalive_running;
    status.watch_running = base.watch_running;
    status.keepalive_failure_count = base.keepalive_failure_count;
    status.discovery_recovery_success_count = base.discovery_recovery_success_count;
    status.discovery_recovery_failure_count = base.discovery_recovery_failure_count;
    status.forward_failure_count = mForwardFailureCount;
    status.last_forward_failure_reason = mLastForwardFailureReason;
    status.discovery_runtime = base.discovery_runtime;
    const auto members = mDiscovery.All();
    status.member_count = base.member_count;
    status.visible_game_members = static_cast<std::size_t>(std::count_if(
        members.begin(),
        members.end(),
        [](const ipc::ProcessDescriptor& member) {
            return member.process.process_id.service_type == kGameServiceType;
        }));
    const auto healthy_links = mLinkManager ? mLinkManager->GetHealthyLinks() : std::vector<ipc::ProcessRef>{};
    status.healthy_game_links = static_cast<std::size_t>(std::count_if(
        healthy_links.begin(),
        healthy_links.end(),
        [](const ipc::ProcessRef& link) {
            return link.process_id.service_type == kGameServiceType;
        }));
    status.auto_connect_targets = base.auto_connect_targets;
    status.auto_connect_success_count = base.auto_connect_success_count;
    status.auto_connect_failure_count = base.auto_connect_failure_count;
    if (base.has_last_auto_connect_target)
    {
        status.has_last_auto_connect_target = true;
        status.last_auto_connect_target = base.last_auto_connect_target;
    }
    if (base.has_last_auto_connect_failure_target)
    {
        status.has_last_auto_connect_failure_target = true;
        status.last_auto_connect_failure_target = base.last_auto_connect_failure_target;
    }
    status.last_auto_connect_failure_reason = base.last_auto_connect_failure_reason;
    status.forwarded_data_frame_count = mForwardedDataFrameCount.load();
    status.last_error = base.last_error;
    return status;
}

ipc::Result RelayIpcService::ConnectToMember(const ipc::ServiceType service_type, const ipc::InstanceId instance_id)
{
    std::scoped_lock lock(mMutex);
    const auto member = FindDiscoveredMemberLocked(service_type, instance_id);
    if (!member.has_value())
    {
        return ipc::Result::Failure("target member not found in discovery snapshot");
    }
    return ConnectDiscoveredMemberLocked(*member);
}

ipc::ProcessDescriptor RelayIpcService::BuildSelfDescriptor() const
{
    ipc::ProcessDescriptor self;
    self.process.process_id.service_type = mRelayServiceType;
    self.process.process_id.instance_id = mConfiguration.instance_id;
    self.process.incarnation_id = 1;
    self.service_name = "relay";
    self.listen_endpoint.host = mConfiguration.listen.host;
    self.listen_endpoint.port = mConfiguration.listen.port;
    self.protocol_version = 1;
    self.start_time_unix_ms = 0;
    self.relay_capabilities.push_back(mRelayServiceType);
    self.labels.emplace_back("role", "relay");
    return self;
}

ipc::Result RelayIpcService::SetupRoleComponentsLocked()
{
    google::protobuf::StringValue sample_message;
    return mPayloadRegistry.Register(sample_message);
}

void RelayIpcService::HandleIncomingDataFrameLocked(const ipc::RawFrame& frame)
{
    if (!mMessenger)
    {
        return;
    }
    if (!IsIpcActiveLocked())
    {
        RecordForwardFailureLocked("ipc is not active");
        return;
    }
    const ipc::Result handle_result = mMessenger->HandleIncomingFrame(frame);
    if (handle_result.ok)
    {
        ++mForwardedDataFrameCount;
    }
    else
    {
        RecordForwardFailureLocked(handle_result.message);
    }
}

void RelayIpcService::HandleDiscoveryFailureLockedExtra(const std::string&)
{
    mAutoConnectAttempts.clear();
}

void RelayIpcService::OnDiscoveryRecovered()
{
    ReconcileAutoConnectMembers();
}

void RelayIpcService::TryAutoConnectMember(const ipc::ProcessDescriptor& member)
{
    std::scoped_lock lock(mMutex);
    if (!mSelf.has_value() || !mTransport || !mLinkManager)
    {
        return;
    }
    if (!IsIpcActiveLocked())
    {
        return;
    }
    if (!some_server::common::FirstPhaseIpcTopologyPolicy::ShouldRelayAutoConnectTarget(
            mSelf->process,
            member,
            kGameServiceType,
            mLinkManager->HasHealthyDirectLink(member.process)))
    {
        return;
    }

    const std::uint64_t key = MakeProcessKey(member.process.process_id);
    if (mAutoConnectAttempts.contains(key))
    {
        return;
    }

    const ipc::Result connect_result = ConnectDiscoveredMemberLocked(member);
    if (!connect_result.ok)
    {
        RecordAutoConnectFailureLocked(member, connect_result, "relay ipc");
        return;
    }

    RecordAutoConnectSuccessLocked(member, "relay ipc");
}

void RelayIpcService::RecordForwardFailureLocked(const std::string& reason)
{
    ++mForwardFailureCount;
    mLastForwardFailureReason = reason;
    mLastError = reason;
}
