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
    : ServiceBase("relay_ipc", kRelayIpcBatch)
    , mConfiguration(configuration)
    , mRelayServiceType(relay_service_type)
    , mRoutingPolicy(kRelayServiceType)
    , mRouter(mRoutingPolicy)
    , mDiscovery(ipc::EtcdDiscoveryOptions{
          .endpoints = configuration.discovery.endpoints,
          .prefix = configuration.discovery.prefix,
          .lease_ttl_seconds = configuration.discovery.lease_ttl_seconds})
{
}

LifecycleTask RelayIpcService::Load()
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
                return;
            }

            if (mLinkManager)
            {
                (void)mLinkManager->OnFrame(frame);
            }
        });
    google::protobuf::StringValue sample_message;
    (void)mPayloadRegistry.Register(sample_message);
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

LifecycleTask RelayIpcService::Start()
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
            spdlog::warn("relay ipc transport listen failed: {}", mLastError);
            return LifecycleTask::Completed();
        }
        mTransportReady = true;

        if (const ipc::Result register_result = mDiscovery.RegisterSelf(*mSelf); !register_result.ok)
        {
            mRegistered = false;
            mIpcReady = false;
            mLastError = register_result.message;
            spdlog::warn("relay ipc discovery register failed: {}", mLastError);
            return LifecycleTask::Completed();
        }

        mRegistered = true;
        mIpcReady = true;
        mLastError.clear();

        if (const ipc::Result refresh_result = mDiscovery.RefreshSnapshot(); !refresh_result.ok)
        {
            mLastError = refresh_result.message;
            spdlog::warn("relay ipc discovery refresh failed: {}", mLastError);
        }
        if (const ipc::Result watch_result = mDiscovery.StartWatch(); !watch_result.ok)
        {
            mLastError = watch_result.message;
            spdlog::warn("relay ipc discovery watch failed: {}", mLastError);
        }
    }

    StartKeepAliveLoop();
    ReconcileAutoConnectMembers();
    StartAutoConnectLoop();

    return LifecycleTask::Completed();
}

LifecycleTask RelayIpcService::Stop()
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
            spdlog::warn("relay ipc discovery remove failed: {}", mLastError);
        }
    }

    mRegistered = false;
    mIpcReady = false;
    return LifecycleTask::Completed();
}

LifecycleTask RelayIpcService::Unload()
{
    StopAutoConnectLoop();
    StopKeepAliveLoop();
    std::scoped_lock lock(mMutex);
    mMessenger.reset();
    mTransportMessageSender.reset();
    mDiscovery.StopWatch();
    mLinkManager.reset();
    mTransport.reset();
    mSelf.reset();
    mTransportReady = false;
    mIpcReady = false;
    return LifecycleTask::Completed();
}

RelayIpcStatus RelayIpcService::Snapshot() const
{
    std::scoped_lock lock(mMutex);
    RelayIpcStatus status;
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
    status.keepalive_failure_count = mKeepAliveFailureCount;
    status.discovery_recovery_success_count = mDiscoveryRecoverySuccessCount;
    status.discovery_recovery_failure_count = mDiscoveryRecoveryFailureCount;
    status.forward_failure_count = mForwardFailureCount;
    status.last_forward_failure_reason = mLastForwardFailureReason;
    status.discovery_runtime = mDiscovery.RuntimeStats();
    const auto members = mDiscovery.All();
    status.member_count = members.size();
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
    status.forwarded_data_frame_count = mForwardedDataFrameCount.load();
    status.last_error = mLastError;
    return status;
}

ipc::Result RelayIpcService::RefreshDiscovery()
{
    const ipc::Result refresh_result = mDiscovery.RefreshSnapshot();
    std::scoped_lock lock(mMutex);
    if (!refresh_result.ok)
    {
        mLastError = refresh_result.message;
        return refresh_result;
    }

    mLastError.clear();
    return ipc::Result::Success();
}

ipc::Result RelayIpcService::KeepAliveOnce()
{
    const ipc::Result keepalive_result = mDiscovery.KeepAliveOnce();
    std::scoped_lock lock(mMutex);
    if (!keepalive_result.ok)
    {
        mLastError = keepalive_result.message;
        return keepalive_result;
    }

    mLastError.clear();
    return ipc::Result::Success();
}

std::vector<ipc::MembershipEvent> RelayIpcService::DrainMembershipEvents()
{
    std::scoped_lock lock(mMutex);
    return mDiscovery.DrainEvents();
}

std::vector<ipc::ProcessDescriptor> RelayIpcService::Members() const
{
    std::scoped_lock lock(mMutex);
    return mDiscovery.All();
}

std::vector<ipc::ProcessRef> RelayIpcService::HealthyLinks() const
{
    std::scoped_lock lock(mMutex);
    if (!mLinkManager)
    {
        return {};
    }
    return mLinkManager->GetHealthyLinks();
}

ipc::Result RelayIpcService::ConnectToMember(const ipc::ServiceType service_type, const ipc::InstanceId instance_id)
{
    std::scoped_lock lock(mMutex);
    if (!mTransport)
    {
        return ipc::Result::Failure("transport is not initialized");
    }

    for (const auto& member : mDiscovery.All())
    {
        if (member.process.process_id.service_type == service_type &&
            member.process.process_id.instance_id == instance_id &&
            (!mSelf.has_value() || member.process != mSelf->process))
        {
            return mTransport->Connect(member.listen_endpoint);
        }
    }

    return ipc::Result::Failure("target member not found in discovery snapshot");
}

std::uint64_t RelayIpcService::MakeProcessKey(const ipc::ProcessId& id)
{
    return (static_cast<std::uint64_t>(id.service_type) << 32) | id.instance_id;
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

void RelayIpcService::FlushLinkFrames()
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

void RelayIpcService::StartKeepAliveLoop()
{
    if (mConfiguration.discovery.lease_ttl_seconds == 0 || mKeepAliveThread.joinable())
    {
        return;
    }

    mStopKeepAlive = false;
    const std::uint32_t interval_seconds = std::max(1u, mConfiguration.discovery.lease_ttl_seconds / 2);
    mKeepAliveThread = std::thread(&RelayIpcService::KeepAliveLoop, this, interval_seconds);
}

void RelayIpcService::StopKeepAliveLoop()
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

void RelayIpcService::KeepAliveLoop(const std::uint32_t interval_seconds)
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
            lock.unlock();
            const ipc::Result recover_result = TryRecoverDiscovery();
            lock.lock();
            if (recover_result.ok)
            {
                ++mDiscoveryRecoverySuccessCount;
                spdlog::info("relay ipc discovery recovered");
                continue;
            }

            ++mDiscoveryRecoveryFailureCount;
            mLastError = recover_result.message;
            spdlog::warn("relay ipc discovery recovery failed: {}", mLastError);
            continue;
        }

        lock.unlock();
        const ipc::Result keepalive_result = mDiscovery.KeepAliveOnce();
        lock.lock();
        if (!keepalive_result.ok)
        {
            ++mKeepAliveFailureCount;
            HandleDiscoveryFailureLocked(keepalive_result.message);
            spdlog::warn("relay ipc discovery keepalive failed: {}", mLastError);
        }
    }

    mKeepAliveRunning.store(false);
}

ipc::Result RelayIpcService::TryRecoverDiscovery()
{
    std::optional<ipc::ProcessDescriptor> self;
    {
        std::scoped_lock lock(mMutex);
        if (!mTransportReady || !mSelf.has_value())
        {
            return ipc::Result::Failure("transport/self are not ready for discovery recovery");
        }
        self = mSelf;
    }

    if (const ipc::Result register_result = mDiscovery.RegisterSelf(*self); !register_result.ok)
    {
        return register_result;
    }
    if (const ipc::Result refresh_result = mDiscovery.RefreshSnapshot(); !refresh_result.ok)
    {
        return refresh_result;
    }
    if (const ipc::Result watch_result = mDiscovery.StartWatch(); !watch_result.ok)
    {
        return watch_result;
    }

    {
        std::scoped_lock lock(mMutex);
        mRegistered = true;
        mIpcReady = true;
        mLastError.clear();
    }

    ReconcileAutoConnectMembers();
    return ipc::Result::Success();
}

void RelayIpcService::StartAutoConnectLoop()
{
    if (mAutoConnectThread.joinable())
    {
        return;
    }

    mStopAutoConnect = false;
    mAutoConnectThread = std::thread(&RelayIpcService::AutoConnectLoop, this);
}

void RelayIpcService::StopAutoConnectLoop()
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

void RelayIpcService::AutoConnectLoop()
{
    while (true)
    {
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
        }

        const auto events = DrainMembershipEvents();
        for (const auto& event : events)
        {
            HandleMembershipEvent(event);
        }
    }
}

void RelayIpcService::ReconcileAutoConnectMembers()
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

void RelayIpcService::HandleMembershipEvent(const ipc::MembershipEvent& event)
{
    if (event.type == ipc::MembershipEventType::removed)
    {
        std::scoped_lock lock(mMutex);
        mAutoConnectAttempts.erase(MakeProcessKey(event.process.process.process_id));
        return;
    }

    TryAutoConnectMember(event.process);
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

    const ipc::Result connect_result = mTransport->Connect(member.listen_endpoint);
    if (!connect_result.ok)
    {
        ++mAutoConnectFailureCount;
        mLastAutoConnectFailureTarget = member.process;
        mLastAutoConnectFailureReason = connect_result.message;
        mLastError = connect_result.message;
        spdlog::warn(
            "relay ipc auto-connect failed: service_type={} instance_id={} error={}",
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
        "relay ipc auto-connect: service_type={} instance_id={} host={} port={}",
        member.process.process_id.service_type,
        member.process.process_id.instance_id,
        member.listen_endpoint.host,
        member.listen_endpoint.port);
}

bool RelayIpcService::IsIpcActiveLocked() const
{
    return mRegistered && mIpcReady;
}

void RelayIpcService::HandleDiscoveryFailureLocked(const std::string& message)
{
    mRegistered = false;
    mIpcReady = false;
    mLastError = message;
    mAutoConnectAttempts.clear();
}

void RelayIpcService::RecordForwardFailureLocked(const std::string& reason)
{
    ++mForwardFailureCount;
    mLastForwardFailureReason = reason;
    mLastError = reason;
}
