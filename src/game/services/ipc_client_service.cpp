#include "ipc_client_service.h"

#include "../../common/ipc/first_phase_topology_policy.h"

#include <ipc/common.pb.h>
#include <ipc/login.pb.h>
#include <ipc/player_message.pb.h>
#include <ipc/push.pb.h>
#include <ipc/session.pb.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace
{
constexpr std::int32_t kGameIpcBatch = 100;
constexpr ipc::ServiceType kRelayServiceType = 99;
}

GameIpcClientService::GameIpcClientService(const GameConfiguration& configuration, ipc::ServiceType game_service_type)
    : IpcNodeServiceBase(
          "game_ipc_client",
          kGameIpcBatch,
          ipc::EtcdDiscoveryOptions{
              .endpoints = configuration.discovery.endpoints,
              .prefix = configuration.discovery.prefix,
              .lease_ttl_seconds = configuration.discovery.lease_ttl_seconds},
          kRelayServiceType)
    , mConfiguration(configuration)
    , mGameServiceType(game_service_type)
    , mServiceReceiverHost(game_service_type)
{
}

LifecycleTask GameIpcClientService::Load()
{
    return LoadIpcRuntime();
}

LifecycleTask GameIpcClientService::Start()
{
    (void)StartIpcRuntime();
    ReconcileAutoConnectMembers();
    StartAutoConnectLoop();

    return LifecycleTask::Completed();
}

LifecycleTask GameIpcClientService::Stop()
{
    return StopIpcRuntime();
}

LifecycleTask GameIpcClientService::Unload()
{
    return UnloadIpcRuntime();
}

GameIpcClientStatus GameIpcClientService::Snapshot() const
{
    std::scoped_lock lock(mMutex);
    const auto base = SnapshotBaseStatusLocked();
    GameIpcClientStatus status;
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
    status.player_dispatch_count = mPlayerReceiverHost.DispatchCount();
    status.last_player_id = mPlayerReceiverHost.LastPlayerId();
    status.last_player_payload_type = mPlayerReceiverHost.LastPayloadType();
    status.local_service_dispatch_count = mServiceReceiverHost.DispatchCount();
    status.last_payload_type = mServiceReceiverHost.LastPayloadType();
    status.last_error = base.last_error;
    return status;
}

GameLocalReceiverSnapshot GameIpcClientService::LocalReceivers() const
{
    std::scoped_lock lock(mMutex);
    GameLocalReceiverSnapshot snapshot;
    if (mSelf.has_value())
    {
        snapshot.process_receiver = mSelf->process;
    }
    snapshot.service_receiver = LocalServiceReceiverAddress();
    snapshot.local_player_ids = mPlayerReceiverHost.BoundPlayers();
    std::sort(snapshot.local_player_ids.begin(), snapshot.local_player_ids.end());
    return snapshot;
}

ipc::Result GameIpcClientService::ConnectToProcess(const ipc::InstanceId instance_id)
{
    std::scoped_lock lock(mMutex);
    const auto member = FindDiscoveredMemberLocked(mGameServiceType, instance_id);
    if (!member.has_value())
    {
        return ipc::Result::Failure("target process not found in discovery snapshot");
    }
    return ConnectDiscoveredMemberLocked(*member);
}

ipc::Result GameIpcClientService::BindLocalPlayer(const std::uint64_t player_id)
{
    std::scoped_lock lock(mMutex);
    if (!mSelf.has_value())
    {
        return ipc::Result::Failure("self descriptor is not initialized");
    }
    if (!mPlayerReceiverHost.Bind(player_id))
    {
        return ipc::Result::Failure("player is already bound locally");
    }
    const ipc::Result bind_result = mReceiverDirectory.Bind(PlayerReceiverAddress(player_id), mSelf->process);
    if (!bind_result.ok)
    {
        (void)mPlayerReceiverHost.Unbind(player_id);
    }
    return bind_result;
}

ipc::Result GameIpcClientService::UnbindLocalPlayer(const std::uint64_t player_id)
{
    std::scoped_lock lock(mMutex);
    if (!mSelf.has_value())
    {
        return ipc::Result::Failure("self descriptor is not initialized");
    }
    if (!mPlayerReceiverHost.IsBound(player_id))
    {
        return ipc::Result::Failure("player is not bound locally");
    }

    const auto receiver = PlayerReceiverAddress(player_id);
    const auto location = mReceiverDirectory.Resolve(receiver);
    if (location.kind != ipc::ReceiverLocationKind::single_process || location.processes.size() != 1)
    {
        return ipc::Result::Failure("player receiver location is not locally resolvable");
    }
    if (location.processes[0] != mSelf->process)
    {
        return ipc::Result::Failure("player receiver owner mismatch");
    }

    const ipc::Result invalidate_result = mReceiverDirectory.Invalidate(receiver, mSelf->process, location.version);
    if (!invalidate_result.ok)
    {
        return invalidate_result;
    }
    (void)mPlayerReceiverHost.Unbind(player_id);
    return ipc::Result::Success();
}

ipc::Result GameIpcClientService::BindRemotePlayer(const std::uint64_t player_id, const ipc::InstanceId instance_id)
{
    std::scoped_lock lock(mMutex);
    if (!mSelf.has_value())
    {
        return ipc::Result::Failure("self descriptor is not initialized");
    }

    for (const auto& member : mDiscovery.All())
    {
        if (member.process.process_id.service_type == mGameServiceType &&
            member.process.process_id.instance_id == instance_id)
        {
            return mReceiverDirectory.Bind(PlayerReceiverAddress(player_id), member.process);
        }
    }

    return ipc::Result::Failure("target player owner not found in discovery snapshot");
}

ipc::SendResult GameIpcClientService::SendLocalServiceMessage(const std::string& value)
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

    google::protobuf::StringValue payload;
    payload.set_value(value);
    const ipc::SendResult result = mMessenger->SendToReceiver(LocalServiceReceiverAddress(), payload);
    if (!result.ok)
    {
        RecordSendRejectLocked(result.message);
    }
    return result;
}

ipc::SendResult GameIpcClientService::SendProcessPayload(const ipc::ProcessId target, const google::protobuf::Message& message)
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

ipc::SendResult GameIpcClientService::SendProcessMessage(const ipc::InstanceId instance_id, const std::string& value)
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

    google::protobuf::StringValue payload;
    payload.set_value(value);
    const ipc::SendResult result = mMessenger->SendToProcess({mGameServiceType, instance_id}, payload);
    if (!result.ok)
    {
        RecordSendRejectLocked(result.message);
    }
    return result;
}

ipc::SendResult GameIpcClientService::SendPlayerMessage(const std::uint64_t player_id, const std::string& value)
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

    google::protobuf::StringValue payload;
    payload.set_value(value);
    const ipc::SendResult result = mMessenger->SendToReceiver(PlayerReceiverAddress(player_id), payload);
    if (!result.ok)
    {
        RecordSendRejectLocked(result.message);
    }
    return result;
}

ipc::SendResult GameIpcClientService::BroadcastServiceMessage(const std::string& value, const bool include_local)
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

    google::protobuf::StringValue payload;
    payload.set_value(value);
    ipc::BroadcastScope scope;
    scope.service_type = mGameServiceType;
    scope.include_local = include_local;
    const ipc::SendResult result = mMessenger->BroadcastToService(mGameServiceType, scope, payload);
    if (!result.ok)
    {
        RecordSendRejectLocked(result.message);
    }
    return result;
}

ipc::ProcessDescriptor GameIpcClientService::BuildSelfDescriptor() const
{
    ipc::ProcessDescriptor self;
    self.process.process_id.service_type = mGameServiceType;
    self.process.process_id.instance_id = mConfiguration.instance_id;
    self.process.incarnation_id = 1;
    self.service_name = "game";
    self.listen_endpoint.host = mConfiguration.listen.host;
    self.listen_endpoint.port = mConfiguration.listen.port;
    self.protocol_version = 1;
    self.start_time_unix_ms = 0;
    self.labels.emplace_back("role", "game");
    return self;
}

ipc::Result GameIpcClientService::SetupRoleComponentsLocked()
{
    mProcessReceiverHost = std::make_unique<ProcessReceiverHost>(mSelf->process);
    mProcessReceiverHost->SetDispatchHandler(
        [this](const ipc::ReceiverAddress& target, const ipc::Envelope& envelope) {
            return mProcessDispatcher.Dispatch(envelope, target);
        });
    if (const ipc::Result host_result = mReceiverRegistry.Register(*mProcessReceiverHost, ipc::ReceiverType::process); !host_result.ok)
    {
        mLastError = host_result.message;
        spdlog::warn("game ipc process receiver host register failed: {}", mLastError);
        return host_result;
    }
    if (const ipc::Result host_result = mReceiverRegistry.Register(mPlayerReceiverHost, ipc::ReceiverType::player); !host_result.ok)
    {
        mLastError = host_result.message;
        spdlog::warn("game ipc player receiver host register failed: {}", mLastError);
        return host_result;
    }
    const auto receiver = LocalServiceReceiverAddress();
    if (const ipc::Result bind_result = mReceiverDirectory.Bind(receiver, mSelf->process); !bind_result.ok)
    {
        mLastError = bind_result.message;
        spdlog::warn("game ipc receiver bind failed: {}", mLastError);
        return bind_result;
    }
    if (const ipc::Result host_result = mReceiverRegistry.Register(mServiceReceiverHost, ipc::ReceiverType::service); !host_result.ok)
    {
        mLastError = host_result.message;
        spdlog::warn("game ipc receiver host register failed: {}", mLastError);
        return host_result;
    }

    google::protobuf::StringValue sample_message;
    pb::ipc::LoginPlayerRequest login_request;
    pb::ipc::LoginPlayerResponse login_response;
    pb::ipc::ReconnectPlayerRequest reconnect_request;
    pb::ipc::ReconnectPlayerResponse reconnect_response;
    pb::ipc::KickAccountSession kick_request;
    pb::ipc::KickAccountSessionAck kick_ack;
    pb::ipc::PlayerDisconnected disconnected;
    pb::ipc::BindPlayerSession bind_session;
    pb::ipc::UnbindPlayerSession unbind_session;
    pb::ipc::ForwardPlayerMessageRequest player_message_request;
    pb::ipc::ForwardPlayerMessageResponse player_message_response;
    pb::ipc::PushPlayerMessage push_message;

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
            spdlog::warn("game ipc payload register failed: {}", mLastError);
            return payload_result;
        }
    }

    return ipc::Result::Success();
}

void GameIpcClientService::TeardownRoleComponentsLocked()
{
    mProcessReceiverHost.reset();
}

void GameIpcClientService::HandleIncomingDataFrameLocked(const ipc::RawFrame& frame)
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

void GameIpcClientService::HandleDiscoveryFailureLockedExtra(const std::string&)
{
    mAutoConnectAttempts.clear();
}

void GameIpcClientService::OnDiscoveryRecovered()
{
    ReconcileAutoConnectMembers();
}

ipc::ReceiverAddress GameIpcClientService::LocalServiceReceiverAddress() const
{
    return ipc::ReceiverAddress{
        .type = ipc::ReceiverType::service,
        .key_hi = mGameServiceType,
        .key_lo = 1};
}

ipc::ReceiverAddress GameIpcClientService::PlayerReceiverAddress(const std::uint64_t player_id)
{
    return ipc::ReceiverAddress{
        .type = ipc::ReceiverType::player,
        .key_hi = player_id,
        .key_lo = 0};
}

bool GameIpcClientService::ShouldRefreshAutoConnectLocked() const
{
    const auto members = mDiscovery.All();
    const auto healthy_links = mLinkManager ? mLinkManager->GetHealthyLinks() : std::vector<ipc::ProcessRef>{};
    return some_server::common::FirstPhaseIpcTopologyPolicy::ShouldGameReconcile(
        some_server::common::FirstPhaseIpcTopologyPolicy::HasMemberOfServiceType(members, kRelayServiceType),
        some_server::common::FirstPhaseIpcTopologyPolicy::HasHealthyLinkOfServiceType(
            healthy_links,
            kRelayServiceType));
}

void GameIpcClientService::TryAutoConnectMember(const ipc::ProcessDescriptor& member)
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
        RecordAutoConnectFailureLocked(member, connect_result, "game ipc");
        return;
    }

    RecordAutoConnectSuccessLocked(member, "game ipc");
}

bool GameIpcClientService::HasHealthyRelayLink() const
{
    if (!mLinkManager)
    {
        return false;
    }

    return some_server::common::FirstPhaseIpcTopologyPolicy::HasHealthyLinkOfServiceType(
        mLinkManager->GetHealthyLinks(),
        kRelayServiceType);
}

bool GameIpcClientService::HasRelayMemberInDiscoveryLocked() const
{
    return some_server::common::FirstPhaseIpcTopologyPolicy::HasMemberOfServiceType(
        mDiscovery.All(),
        kRelayServiceType);
}

void GameIpcClientService::RecordSendRejectLocked(const std::string& reason)
{
    ++mSendRejectCount;
    mLastSendRejectReason = reason;
    mLastError = reason;
}
