#include "ipc_service.h"

#include "../../common/ipc/first_phase_topology_policy.h"

#include <google/protobuf/wrappers.pb.h>
#include <ipc/gate_game/v1/common.pb.h>
#include <ipc/gate_game/v1/login.pb.h>
#include <ipc/gate_game/v1/player_message.pb.h>
#include <ipc/gate_game/v1/push.pb.h>
#include <ipc/gate_game/v1/session.pb.h>
#include <spdlog/spdlog.h>

#include <algorithm>

namespace
{
constexpr std::int32_t kGateIpcBatch = 100;
constexpr ipc::ServiceType kRelayServiceType = 99;
}

GateIpcService::GateIpcService(const GateConfiguration& configuration, ipc::ServiceType gate_service_type)
    : IpcNodeServiceBase(
          "gate_ipc",
          kGateIpcBatch,
          ipc::EtcdDiscoveryOptions{
              .endpoints = configuration.discovery.endpoints,
              .prefix = configuration.discovery.prefix,
              .lease_ttl_seconds = configuration.discovery.lease_ttl_seconds},
          kRelayServiceType)
    , mConfiguration(configuration)
    , mGateServiceType(gate_service_type)
    , mServiceReceiverHost(gate_service_type)
{
}

LifecycleTask GateIpcService::Load()
{
    return LoadIpcRuntime();
}

LifecycleTask GateIpcService::Start()
{
    (void)StartIpcRuntime();
    ReconcileAutoConnectMembers();
    StartAutoConnectLoop();
    return LifecycleTask::Completed();
}

LifecycleTask GateIpcService::Stop()
{
    return StopIpcRuntime();
}

LifecycleTask GateIpcService::Unload()
{
    return UnloadIpcRuntime();
}

GateIpcStatus GateIpcService::Snapshot() const
{
    std::scoped_lock lock(mMutex);
    const auto base = SnapshotBaseStatusLocked();
    GateIpcStatus status;
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
    status.send_reject_count = mSendRejectCount;
    status.last_send_reject_reason = mLastSendRejectReason;
    status.discovery_runtime = base.discovery_runtime;
    const auto members = mDiscovery.All();
    const auto healthy_links = mLinkManager ? mLinkManager->GetHealthyLinks() : std::vector<ipc::ProcessRef>{};
    status.member_count = base.member_count;
    status.relay_member_visible =
        some_server::common::FirstPhaseIpcTopologyPolicy::HasMemberOfServiceType(members, kRelayServiceType);
    status.healthy_relay_link = some_server::common::FirstPhaseIpcTopologyPolicy::HasHealthyLinkOfServiceType(
        healthy_links,
        kRelayServiceType);
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
    status.process_dispatch_count = mProcessReceiverHost ? mProcessReceiverHost->DispatchCount() : 0;
    status.last_process_payload_type =
        mProcessReceiverHost ? mProcessReceiverHost->LastPayloadType() : std::string{};
    status.local_service_dispatch_count = mServiceReceiverHost.DispatchCount();
    status.last_payload_type = mServiceReceiverHost.LastPayloadType();
    status.last_error = base.last_error;
    return status;
}

ipc::SendResult GateIpcService::SendProcessPayload(const ipc::ProcessId target, const google::protobuf::Message& message)
{
    std::scoped_lock lock(mMutex);
    if (!mMessenger)
    {
        const auto result = ipc::SendResult::Failure("messenger is not initialized");
        RecordSendRejectLocked(result.message);
        return result;
    }
    if (!IsIpcActiveLocked())
    {
        const auto result = ipc::SendResult::Failure("ipc is not active");
        RecordSendRejectLocked(result.message);
        return result;
    }

    const ipc::SendResult result = mMessenger->SendToProcess(target, message);
    if (!result.ok)
    {
        RecordSendRejectLocked(result.message);
    }
    return result;
}

ipc::ProcessDescriptor GateIpcService::BuildSelfDescriptor() const
{
    ipc::ProcessDescriptor self;
    self.process.process_id.service_type = mGateServiceType;
    self.process.process_id.instance_id = mConfiguration.instance_id;
    self.process.incarnation_id = 1;
    self.service_name = "gate";
    self.listen_endpoint.host = mConfiguration.listen.host;
    self.listen_endpoint.port = mConfiguration.listen.port;
    self.protocol_version = 1;
    self.start_time_unix_ms = 0;
    self.labels.emplace_back("role", "gate");
    return self;
}

ipc::Result GateIpcService::SetupRoleComponentsLocked()
{
    mProcessReceiverHost = std::make_unique<ProcessReceiverHost>(mSelf->process);
    mProcessReceiverHost->SetDispatchHandler(mProcessDispatchHandler);
    if (const ipc::Result host_result = mReceiverRegistry.Register(*mProcessReceiverHost, ipc::ReceiverType::process);
        !host_result.ok)
    {
        mLastError = host_result.message;
        spdlog::warn("gate ipc process receiver host register failed: {}", mLastError);
        return host_result;
    }

    const auto receiver = LocalServiceReceiverAddress();
    if (const ipc::Result bind_result = mReceiverDirectory.Bind(receiver, mSelf->process); !bind_result.ok)
    {
        mLastError = bind_result.message;
        spdlog::warn("gate ipc receiver bind failed: {}", mLastError);
        return bind_result;
    }

    if (const ipc::Result host_result = mReceiverRegistry.Register(mServiceReceiverHost, ipc::ReceiverType::service);
        !host_result.ok)
    {
        mLastError = host_result.message;
        spdlog::warn("gate ipc receiver host register failed: {}", mLastError);
        return host_result;
    }

    google::protobuf::StringValue sample_message;
    some_server::ipc::gate_game::v1::LoginPlayerRequest login_request;
    some_server::ipc::gate_game::v1::LoginPlayerResponse login_response;
    some_server::ipc::gate_game::v1::ReconnectPlayerRequest reconnect_request;
    some_server::ipc::gate_game::v1::ReconnectPlayerResponse reconnect_response;
    some_server::ipc::gate_game::v1::KickAccountSession kick_request;
    some_server::ipc::gate_game::v1::KickAccountSessionAck kick_ack;
    some_server::ipc::gate_game::v1::PlayerDisconnected disconnected;
    some_server::ipc::gate_game::v1::BindPlayerSession bind_session;
    some_server::ipc::gate_game::v1::UnbindPlayerSession unbind_session;
    some_server::ipc::gate_game::v1::ForwardPlayerMessageRequest player_message_request;
    some_server::ipc::gate_game::v1::ForwardPlayerMessageResponse player_message_response;
    some_server::ipc::gate_game::v1::PushPlayerMessage push_message;

    const google::protobuf::Message* payloads[] = {
        &sample_message,
        &login_request,
        &login_response,
        &reconnect_request,
        &reconnect_response,
        &kick_request,
        &kick_ack,
        &disconnected,
        &bind_session,
        &unbind_session,
        &player_message_request,
        &player_message_response,
        &push_message};
    for (const auto* payload : payloads)
    {
        if (const ipc::Result payload_result = mPayloadRegistry.Register(*payload); !payload_result.ok)
        {
            mLastError = payload_result.message;
            spdlog::warn("gate ipc payload register failed: {}", mLastError);
            return payload_result;
        }
    }

    return ipc::Result::Success();
}

void GateIpcService::TeardownRoleComponentsLocked()
{
    mProcessReceiverHost.reset();
}

void GateIpcService::HandleIncomingDataFrameLocked(const ipc::RawFrame& frame)
{
    ipc::Messenger* messenger = nullptr;
    {
        std::scoped_lock lock(mMutex);
        if (!mMessenger || !IsIpcActiveLocked())
        {
            return;
        }
        messenger = mMessenger.get();
    }

    if (messenger == nullptr)
    {
        return;
    }
    (void)messenger->HandleIncomingFrame(frame);
}

void GateIpcService::HandleDiscoveryFailureLockedExtra(const std::string&)
{
    mAutoConnectAttempts.clear();
}

void GateIpcService::OnDiscoveryRecovered()
{
    ReconcileAutoConnectMembers();
}

bool GateIpcService::ShouldRefreshAutoConnectLocked() const
{
    const auto members = mDiscovery.All();
    const auto healthy_links = mLinkManager ? mLinkManager->GetHealthyLinks() : std::vector<ipc::ProcessRef>{};
    return some_server::common::FirstPhaseIpcTopologyPolicy::ShouldGameReconcile(
        some_server::common::FirstPhaseIpcTopologyPolicy::HasMemberOfServiceType(members, kRelayServiceType),
        some_server::common::FirstPhaseIpcTopologyPolicy::HasHealthyLinkOfServiceType(
            healthy_links,
            kRelayServiceType));
}

void GateIpcService::TryAutoConnectMember(const ipc::ProcessDescriptor& member)
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
    if (!some_server::common::FirstPhaseIpcTopologyPolicy::ShouldGameAutoConnectTarget(
            mSelf->process,
            member,
            kRelayServiceType,
            some_server::common::FirstPhaseIpcTopologyPolicy::HasHealthyLinkOfServiceType(
                mLinkManager->GetHealthyLinks(),
                kRelayServiceType)))
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
        RecordAutoConnectFailureLocked(member, connect_result, "gate ipc");
        return;
    }

    RecordAutoConnectSuccessLocked(member, "gate ipc");
}

ipc::ReceiverAddress GateIpcService::LocalServiceReceiverAddress() const
{
    return ipc::ReceiverAddress{
        .type = ipc::ReceiverType::service,
        .key_hi = mGateServiceType,
        .key_lo = 1};
}

void GateIpcService::RecordSendRejectLocked(const std::string& reason)
{
    ++mSendRejectCount;
    mLastSendRejectReason = reason;
    mLastError = reason;
}

void GateIpcService::SetProcessDispatchHandler(ProcessDispatchHandler handler)
{
    std::scoped_lock lock(mMutex);
    mProcessDispatchHandler = std::move(handler);
    if (mProcessReceiverHost)
    {
        mProcessReceiverHost->SetDispatchHandler(mProcessDispatchHandler);
    }
}
