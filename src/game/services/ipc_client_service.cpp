#include "ipc_client_service.h"

#include "../../common/ipc/first_phase_topology_policy.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace
{
constexpr std::int32_t kGameIpcBatch = 100;
constexpr ipc::ServiceType kRelayServiceType = 99;
}

GameIpcClientService::GameIpcClientService(const GameConfiguration& configuration, ipc::ServiceType game_service_type)
    : ServiceBase("game_ipc_client", kGameIpcBatch)
    , mConfiguration(configuration)
    , mGameServiceType(game_service_type)
    , mRoutingPolicy(kRelayServiceType)
    , mRouter(mRoutingPolicy)
    , mDiscovery(ipc::EtcdDiscoveryOptions{
          .etcdctl_path = "etcdctl",
          .endpoints = configuration.discovery.endpoints,
          .prefix = configuration.discovery.prefix,
          .lease_ttl_seconds = configuration.discovery.lease_ttl_seconds})
    , mServiceReceiverHost(game_service_type)
{
}

LifecycleTask GameIpcClientService::Load()
{
    std::scoped_lock lock(mMutex);
    mSelf = BuildSelfDescriptor();
    mTransport = std::make_unique<ipc::TcpTransport>();
    mLinkManager = std::make_unique<ipc::LinkManager>(mSelf->process);
    mTransport->SetConnectionEventHandler(
        [this](const ipc::ConnectionEvent& event) {
            if (mLinkManager)
            {
                mLinkManager->OnConnectionEvent(event);
                FlushLinkFrames();
            }
        });
    mTransport->SetFrameHandler(
        [this](const ipc::RawFrame& frame) {
            if (!mLinkManager)
            {
                return;
            }

            if (frame.header.kind == ipc::FrameKind::control)
            {
                (void)mLinkManager->OnFrame(frame);
                FlushLinkFrames();
                return;
            }

            if (frame.header.kind == ipc::FrameKind::data && mMessenger)
            {
                std::scoped_lock lock(mMutex);
                if (!IsIpcActiveLocked())
                {
                    return;
                }
                (void)mMessenger->HandleIncomingFrame(frame);
                return;
            }

            if (mLinkManager)
            {
                (void)mLinkManager->OnFrame(frame);
            }
        });
    mProcessReceiverHost = std::make_unique<ProcessReceiverHost>(mSelf->process);
    if (const ipc::Result host_result = mReceiverRegistry.Register(*mProcessReceiverHost, ipc::ReceiverType::process); !host_result.ok)
    {
        mLastError = host_result.message;
        spdlog::warn("game ipc process receiver host register failed: {}", mLastError);
        return LifecycleTask::Completed();
    }
    if (const ipc::Result host_result = mReceiverRegistry.Register(mPlayerReceiverHost, ipc::ReceiverType::player); !host_result.ok)
    {
        mLastError = host_result.message;
        spdlog::warn("game ipc player receiver host register failed: {}", mLastError);
        return LifecycleTask::Completed();
    }
    const auto receiver = LocalServiceReceiverAddress();
    if (const ipc::Result bind_result = mReceiverDirectory.Bind(receiver, mSelf->process); !bind_result.ok)
    {
        mLastError = bind_result.message;
        spdlog::warn("game ipc receiver bind failed: {}", mLastError);
        return LifecycleTask::Completed();
    }
    if (const ipc::Result host_result = mReceiverRegistry.Register(mServiceReceiverHost, ipc::ReceiverType::service); !host_result.ok)
    {
        mLastError = host_result.message;
        spdlog::warn("game ipc receiver host register failed: {}", mLastError);
        return LifecycleTask::Completed();
    }

    google::protobuf::StringValue sample_message;
    if (const ipc::Result payload_result = mPayloadRegistry.Register(sample_message); !payload_result.ok)
    {
        mLastError = payload_result.message;
        spdlog::warn("game ipc payload register failed: {}", mLastError);
        return LifecycleTask::Completed();
    }

    mTransportMessageSender = std::make_unique<ipc::TransportMessageSender>(*mTransport, *mLinkManager);
    mMessenger = std::make_unique<ipc::Messenger>(
        mSelf->process,
        mRouter,
        mReceiverDirectory,
        mReceiverRegistry,
        mPayloadRegistry,
        &mDiscovery,
        mLinkManager.get(),
        mTransportMessageSender.get());
    mRegistered = false;
    mTransportReady = false;
    mIpcReady = false;
    mLastError.clear();
    return LifecycleTask::Completed();
}

LifecycleTask GameIpcClientService::Start()
{
    {
        std::scoped_lock lock(mMutex);
        if (!mSelf.has_value())
        {
            mLastError = "self descriptor is not initialized";
            return LifecycleTask::Completed();
        }
        if (!mTransport || !mLinkManager)
        {
            mLastError = "transport/link are not initialized";
            return LifecycleTask::Completed();
        }
        if (const ipc::Result listen_result = mTransport->Listen(mSelf->listen_endpoint); !listen_result.ok)
        {
            mLastError = listen_result.message;
            spdlog::warn("game ipc transport listen failed: {}", mLastError);
            return LifecycleTask::Completed();
        }
        mTransportReady = true;

        if (const ipc::Result register_result = mDiscovery.RegisterSelf(*mSelf); !register_result.ok)
        {
            mRegistered = false;
            mIpcReady = false;
            mLastError = register_result.message;
            spdlog::warn("game ipc discovery register failed: {}", mLastError);
            return LifecycleTask::Completed();
        }

        mRegistered = true;
        mIpcReady = true;
        mLastError.clear();

        if (const ipc::Result refresh_result = mDiscovery.RefreshSnapshot(); !refresh_result.ok)
        {
            mLastError = refresh_result.message;
            spdlog::warn("game ipc discovery refresh failed: {}", mLastError);
        }
        if (const ipc::Result watch_result = mDiscovery.StartWatch(); !watch_result.ok)
        {
            mLastError = watch_result.message;
            spdlog::warn("game ipc discovery watch failed: {}", mLastError);
        }
    }
    StartKeepAliveLoop();
    ReconcileAutoConnectMembers();
    StartAutoConnectLoop();

    return LifecycleTask::Completed();
}

LifecycleTask GameIpcClientService::Stop()
{
    StopAutoConnectLoop();
    mDiscovery.StopWatch();
    StopKeepAliveLoop();

    std::scoped_lock lock(mMutex);
    if (mRegistered && mSelf.has_value())
    {
        if (const ipc::Result remove_result = mDiscovery.Remove(mSelf->process.process_id); !remove_result.ok)
        {
            mLastError = remove_result.message;
            spdlog::warn("game ipc discovery remove failed: {}", mLastError);
        }
    }

    mRegistered = false;
    mIpcReady = false;
    return LifecycleTask::Completed();
}

LifecycleTask GameIpcClientService::Unload()
{
    StopAutoConnectLoop();
    StopKeepAliveLoop();
    std::scoped_lock lock(mMutex);
    mDiscovery.StopWatch();
    mLinkManager.reset();
    mTransport.reset();
    mTransportMessageSender.reset();
    mMessenger.reset();
    mSelf.reset();
    mTransportReady = false;
    mIpcReady = false;
    return LifecycleTask::Completed();
}

GameIpcClientStatus GameIpcClientService::Snapshot() const
{
    std::scoped_lock lock(mMutex);
    GameIpcClientStatus status;
    if (mSelf.has_value())
    {
        status.self = *mSelf;
    }
    status.transport_ready = mTransportReady;
    status.registered = mRegistered;
    status.ipc_ready = mIpcReady;
    status.membership_degraded = mTransportReady && !mRegistered && !mIpcReady;
    status.keepalive_running = mKeepAliveRunning.load();
    status.watch_running = mDiscovery.WatchRunning();
    const auto members = mDiscovery.All();
    const auto healthy_links = mLinkManager ? mLinkManager->GetHealthyLinks() : std::vector<ipc::ProcessRef>{};
    status.member_count = members.size();
    status.relay_member_visible =
        some_server::common::FirstPhaseIpcTopologyPolicy::HasMemberOfServiceType(members, kRelayServiceType);
    status.healthy_relay_link = some_server::common::FirstPhaseIpcTopologyPolicy::HasHealthyLinkOfServiceType(
        healthy_links,
        kRelayServiceType);
    status.auto_connect_targets = mAutoConnectAttempts.size();
    status.auto_connect_success_count = mAutoConnectSuccessCount;
    status.auto_connect_failure_count = mAutoConnectFailureCount;
    if (mLastAutoConnectTarget.has_value())
    {
        status.has_last_auto_connect_target = true;
        status.last_auto_connect_target = *mLastAutoConnectTarget;
    }
    if (mLastAutoConnectFailureTarget.has_value())
    {
        status.has_last_auto_connect_failure_target = true;
        status.last_auto_connect_failure_target = *mLastAutoConnectFailureTarget;
    }
    status.last_auto_connect_failure_reason = mLastAutoConnectFailureReason;
    status.process_dispatch_count = mProcessReceiverHost ? mProcessReceiverHost->DispatchCount() : 0;
    status.last_process_payload_type =
        mProcessReceiverHost ? mProcessReceiverHost->LastPayloadType() : std::string{};
    status.player_dispatch_count = mPlayerReceiverHost.DispatchCount();
    status.last_player_id = mPlayerReceiverHost.LastPlayerId();
    status.last_player_payload_type = mPlayerReceiverHost.LastPayloadType();
    status.local_service_dispatch_count = mServiceReceiverHost.DispatchCount();
    status.last_payload_type = mServiceReceiverHost.LastPayloadType();
    status.last_error = mLastError;
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

ipc::Result GameIpcClientService::RefreshDiscovery()
{
    std::scoped_lock lock(mMutex);
    const ipc::Result refresh_result = mDiscovery.RefreshSnapshot();
    if (!refresh_result.ok)
    {
        mLastError = refresh_result.message;
        return refresh_result;
    }

    mLastError.clear();
    return ipc::Result::Success();
}

ipc::Result GameIpcClientService::KeepAliveOnce()
{
    std::scoped_lock lock(mMutex);
    const ipc::Result keepalive_result = mDiscovery.KeepAliveOnce();
    if (!keepalive_result.ok)
    {
        mLastError = keepalive_result.message;
        return keepalive_result;
    }

    mLastError.clear();
    return ipc::Result::Success();
}

std::vector<ipc::MembershipEvent> GameIpcClientService::DrainMembershipEvents()
{
    std::scoped_lock lock(mMutex);
    return mDiscovery.DrainEvents();
}

std::vector<ipc::ProcessDescriptor> GameIpcClientService::Members() const
{
    std::scoped_lock lock(mMutex);
    return mDiscovery.All();
}

std::vector<ipc::ProcessRef> GameIpcClientService::HealthyLinks() const
{
    std::scoped_lock lock(mMutex);
    if (!mLinkManager)
    {
        return {};
    }
    return mLinkManager->GetHealthyLinks();
}

ipc::Result GameIpcClientService::ConnectToProcess(const ipc::InstanceId instance_id)
{
    std::scoped_lock lock(mMutex);
    if (!mTransport)
    {
        return ipc::Result::Failure("transport is not initialized");
    }

    for (const auto& member : mDiscovery.All())
    {
        if (member.process.process_id.service_type == mGameServiceType &&
            member.process.process_id.instance_id == instance_id &&
            member.process != mSelf->process)
        {
            return mTransport->Connect(member.listen_endpoint);
        }
    }

    return ipc::Result::Failure("target process not found in discovery snapshot");
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
        return ipc::SendResult::Failure("messenger is not initialized");
    }
    if (!IsIpcActiveLocked())
    {
        return ipc::SendResult::Failure("ipc is not active");
    }

    google::protobuf::StringValue payload;
    payload.set_value(value);
    return mMessenger->SendToReceiver(LocalServiceReceiverAddress(), payload);
}

ipc::SendResult GameIpcClientService::SendProcessMessage(const ipc::InstanceId instance_id, const std::string& value)
{
    std::scoped_lock lock(mMutex);
    if (!mMessenger)
    {
        return ipc::SendResult::Failure("messenger is not initialized");
    }
    if (!IsIpcActiveLocked())
    {
        return ipc::SendResult::Failure("ipc is not active");
    }

    google::protobuf::StringValue payload;
    payload.set_value(value);
    return mMessenger->SendToProcess({mGameServiceType, instance_id}, payload);
}

ipc::SendResult GameIpcClientService::SendPlayerMessage(const std::uint64_t player_id, const std::string& value)
{
    std::scoped_lock lock(mMutex);
    if (!mMessenger)
    {
        return ipc::SendResult::Failure("messenger is not initialized");
    }
    if (!IsIpcActiveLocked())
    {
        return ipc::SendResult::Failure("ipc is not active");
    }

    google::protobuf::StringValue payload;
    payload.set_value(value);
    return mMessenger->SendToReceiver(PlayerReceiverAddress(player_id), payload);
}

ipc::SendResult GameIpcClientService::BroadcastServiceMessage(const std::string& value, const bool include_local)
{
    std::scoped_lock lock(mMutex);
    if (!mMessenger)
    {
        return ipc::SendResult::Failure("messenger is not initialized");
    }
    if (!IsIpcActiveLocked())
    {
        return ipc::SendResult::Failure("ipc is not active");
    }

    google::protobuf::StringValue payload;
    payload.set_value(value);
    ipc::BroadcastScope scope;
    scope.service_type = mGameServiceType;
    scope.include_local = include_local;
    return mMessenger->BroadcastToService(mGameServiceType, scope, payload);
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

void GameIpcClientService::FlushLinkFrames()
{
    if (!mTransport || !mLinkManager)
    {
        return;
    }

    for (auto& frame : mLinkManager->DrainOutboundFrames())
    {
        (void)mTransport->Send(frame);
    }
}

void GameIpcClientService::StartKeepAliveLoop()
{
    if (mConfiguration.discovery.lease_ttl_seconds == 0 || mKeepAliveThread.joinable())
    {
        return;
    }

    mStopKeepAlive = false;
    const std::uint32_t interval_seconds = std::max(1u, mConfiguration.discovery.lease_ttl_seconds / 2);
    mKeepAliveThread = std::thread(&GameIpcClientService::KeepAliveLoop, this, interval_seconds);
}

void GameIpcClientService::StopKeepAliveLoop()
{
    {
        std::scoped_lock lock(mMutex);
        mStopKeepAlive = true;
    }
    mKeepAliveWakeup.notify_all();
    if (mKeepAliveThread.joinable())
    {
        mKeepAliveThread.join();
    }
}

void GameIpcClientService::KeepAliveLoop(const std::uint32_t interval_seconds)
{
    mKeepAliveRunning.store(true);

    std::unique_lock lock(mMutex);
    while (!mStopKeepAlive)
    {
        if (mKeepAliveWakeup.wait_for(lock, std::chrono::seconds(interval_seconds), [this] { return mStopKeepAlive; }))
        {
            break;
        }

        if (!mRegistered)
        {
            continue;
        }

        const ipc::Result keepalive_result = mDiscovery.KeepAliveOnce();
        if (!keepalive_result.ok)
        {
            HandleDiscoveryFailureLocked(keepalive_result.message);
            spdlog::warn("game ipc discovery keepalive failed: {}", mLastError);
        }
    }

    mKeepAliveRunning.store(false);
}

void GameIpcClientService::StartAutoConnectLoop()
{
    if (mAutoConnectThread.joinable())
    {
        return;
    }

    mStopAutoConnect = false;
    mAutoConnectThread = std::thread(&GameIpcClientService::AutoConnectLoop, this);
}

void GameIpcClientService::StopAutoConnectLoop()
{
    {
        std::scoped_lock lock(mMutex);
        mStopAutoConnect = true;
    }
    mAutoConnectWakeup.notify_all();
    if (mAutoConnectThread.joinable())
    {
        mAutoConnectThread.join();
    }
}

void GameIpcClientService::AutoConnectLoop()
{
    while (true)
    {
        bool needs_reconcile = false;
        {
            std::unique_lock lock(mMutex);
            if (mStopAutoConnect)
            {
                break;
            }
            mAutoConnectWakeup.wait_for(lock, std::chrono::milliseconds(200), [this] {
                return mStopAutoConnect;
            });
            if (mStopAutoConnect)
            {
                break;
            }
            const auto members = mDiscovery.All();
            const auto healthy_links = mLinkManager ? mLinkManager->GetHealthyLinks() : std::vector<ipc::ProcessRef>{};
            needs_reconcile = some_server::common::FirstPhaseIpcTopologyPolicy::ShouldGameReconcile(
                some_server::common::FirstPhaseIpcTopologyPolicy::HasMemberOfServiceType(members, kRelayServiceType),
                some_server::common::FirstPhaseIpcTopologyPolicy::HasHealthyLinkOfServiceType(
                    healthy_links,
                    kRelayServiceType));
        }

        const auto events = DrainMembershipEvents();
        for (const auto& event : events)
        {
            HandleMembershipEvent(event);
        }

        if (!needs_reconcile)
        {
            continue;
        }

    if (const ipc::Result refresh_result = RefreshDiscovery(); !refresh_result.ok)
    {
        spdlog::warn("game ipc auto-connect refresh failed: {}", refresh_result.message);
        continue;
        }

        ReconcileAutoConnectMembers();
    }
}

void GameIpcClientService::ReconcileAutoConnectMembers()
{
    std::vector<ipc::ProcessDescriptor> members;
    {
        std::scoped_lock lock(mMutex);
        members = mDiscovery.All();
    }
    for (const auto& member : members)
    {
        TryAutoConnectMember(member);
    }
}

void GameIpcClientService::HandleMembershipEvent(const ipc::MembershipEvent& event)
{
    if (event.type == ipc::MembershipEventType::removed)
    {
        std::scoped_lock lock(mMutex);
        mAutoConnectAttempts.erase(MakeProcessKey(event.process.process.process_id));
        return;
    }

    TryAutoConnectMember(event.process);
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

    const ipc::Result connect_result = mTransport->Connect(member.listen_endpoint);
    if (!connect_result.ok)
    {
        ++mAutoConnectFailureCount;
        mLastAutoConnectFailureTarget = member.process;
        mLastAutoConnectFailureReason = connect_result.message;
        mLastError = connect_result.message;
        spdlog::warn(
            "game ipc auto-connect failed: service_type={} instance_id={} error={}",
            member.process.process_id.service_type,
            member.process.process_id.instance_id,
            connect_result.message);
        return;
    }

    mAutoConnectAttempts.insert(key);
    ++mAutoConnectSuccessCount;
    mLastAutoConnectTarget = member.process;
    mLastAutoConnectFailureTarget.reset();
    mLastAutoConnectFailureReason.clear();
    spdlog::info(
        "game ipc auto-connect: service_type={} instance_id={} host={} port={}",
        member.process.process_id.service_type,
        member.process.process_id.instance_id,
        member.listen_endpoint.host,
        member.listen_endpoint.port);
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

bool GameIpcClientService::IsIpcActiveLocked() const
{
    return mRegistered && mIpcReady;
}

void GameIpcClientService::HandleDiscoveryFailureLocked(const std::string& message)
{
    mRegistered = false;
    mIpcReady = false;
    mLastError = message;
    mAutoConnectAttempts.clear();
}

std::uint64_t GameIpcClientService::MakeProcessKey(const ipc::ProcessId& id)
{
    return (static_cast<std::uint64_t>(id.service_type) << 32) | id.instance_id;
}
