#include "ipc_node_service_base.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace some_server::common
{
namespace ipc = ::ipc;

namespace
{
std::uint32_t KeepAliveIntervalSeconds(const ipc::EtcdDiscoveryOptions& options)
{
    return options.lease_ttl_seconds == 0 ? 0u : std::max(1u, options.lease_ttl_seconds / 2);
}
} // namespace

IpcNodeCore::IpcNodeCore(ipc::EtcdDiscoveryOptions options, const ipc::ServiceType relay_service_type)
    : routing_policy(relay_service_type)
    , router(routing_policy)
    , discovery(options)
{
    keepalive_interval_seconds = KeepAliveIntervalSeconds(options);
}

IpcNodeServiceBase::IpcNodeServiceBase(
    std::string name,
    const std::int32_t batch,
    ipc::EtcdDiscoveryOptions discovery_options,
    const ipc::ServiceType relay_service_type)
    : ServiceBase(std::move(name), batch)
    , mCore(std::move(discovery_options), relay_service_type)
    , mRoutingPolicy(mCore.routing_policy)
    , mRouter(mCore.router)
    , mTransport(mCore.transport)
    , mLinkManager(mCore.link_manager)
    , mDiscovery(mCore.discovery)
    , mReceiverDirectory(mCore.receiver_directory)
    , mReceiverRegistry(mCore.receiver_registry)
    , mPayloadRegistry(mCore.payload_registry)
    , mTransportMessageSender(mCore.transport_message_sender)
    , mMessenger(mCore.messenger)
    , mSelf(mCore.self)
    , mRegistered(mCore.registered)
    , mTransportReady(mCore.transport_ready)
    , mIpcReady(mCore.ipc_ready)
    , mLastError(mCore.last_error)
    , mMutex(mCore.mutex)
    , mKeepAliveWakeup(mCore.keepalive_wakeup)
    , mKeepAliveThread(mCore.keepalive_thread)
    , mStopKeepAlive(mCore.stop_keepalive)
    , mKeepAliveRunning(mCore.keepalive_running)
    , mKeepAliveFailureCount(mCore.keepalive_failure_count)
    , mDiscoveryRecoverySuccessCount(mCore.discovery_recovery_success_count)
    , mDiscoveryRecoveryFailureCount(mCore.discovery_recovery_failure_count)
    , mAutoConnectWakeup(mCore.auto_connect_wakeup)
    , mAutoConnectThread(mCore.auto_connect_thread)
    , mStopAutoConnect(mCore.stop_auto_connect)
    , mAutoConnectAttempts(mCore.auto_connect_attempts)
    , mAutoConnectSuccessCount(mCore.auto_connect_success_count)
    , mAutoConnectFailureCount(mCore.auto_connect_failure_count)
    , mLastAutoConnectTarget(mCore.last_auto_connect_target)
    , mLastAutoConnectFailureTarget(mCore.last_auto_connect_failure_target)
    , mLastAutoConnectFailureReason(mCore.last_auto_connect_failure_reason)
{
}

LifecycleTask IpcNodeServiceBase::LoadIpcRuntime()
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

            if (frame.header.kind == ipc::FrameKind::data)
            {
                HandleIncomingDataFrameLocked(frame);
                return;
            }

            (void)mLinkManager->OnFrame(frame);
        });

    if (const ipc::Result setup_result = SetupRoleComponentsLocked(); !setup_result.ok)
    {
        mLastError = setup_result.message;
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

LifecycleTask IpcNodeServiceBase::StartIpcRuntime()
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
            spdlog::warn("{} transport listen failed: {}", GetName(), mLastError);
            return LifecycleTask::Completed();
        }
        mTransportReady = true;

        if (const ipc::Result register_result = mDiscovery.RegisterSelf(*mSelf); !register_result.ok)
        {
            mRegistered = false;
            mIpcReady = false;
            mLastError = register_result.message;
            spdlog::warn("{} discovery register failed: {}", GetName(), mLastError);
            return LifecycleTask::Completed();
        }

        mRegistered = true;
        mIpcReady = true;
        mLastError.clear();

        if (const ipc::Result refresh_result = mDiscovery.RefreshSnapshot(); !refresh_result.ok)
        {
            mLastError = refresh_result.message;
            spdlog::warn("{} discovery refresh failed: {}", GetName(), mLastError);
        }
        if (const ipc::Result watch_result = mDiscovery.StartWatch(); !watch_result.ok)
        {
            mLastError = watch_result.message;
            spdlog::warn("{} discovery watch failed: {}", GetName(), mLastError);
        }
    }

    StartKeepAliveLoop();
    return LifecycleTask::Completed();
}

LifecycleTask IpcNodeServiceBase::StopIpcRuntime()
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
            spdlog::warn("{} discovery remove failed: {}", GetName(), mLastError);
        }
    }

    mRegistered = false;
    mIpcReady = false;
    return LifecycleTask::Completed();
}

LifecycleTask IpcNodeServiceBase::UnloadIpcRuntime()
{
    StopAutoConnectLoop();
    StopKeepAliveLoop();
    std::scoped_lock lock(mMutex);
    mDiscovery.StopWatch();
    TeardownRoleComponentsLocked();
    mMessenger.reset();
    mTransportMessageSender.reset();
    mReceiverRegistry.Clear();
    mReceiverDirectory.Clear();
    mPayloadRegistry.Clear();
    mLinkManager.reset();
    mTransport.reset();
    mSelf.reset();
    mTransportReady = false;
    mIpcReady = false;
    mRegistered = false;
    mAutoConnectAttempts.clear();
    mAutoConnectSuccessCount = 0;
    mAutoConnectFailureCount = 0;
    mLastAutoConnectTarget.reset();
    mLastAutoConnectFailureTarget.reset();
    mLastAutoConnectFailureReason.clear();
    return LifecycleTask::Completed();
}

ipc::Result IpcNodeServiceBase::RefreshDiscovery()
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

ipc::Result IpcNodeServiceBase::KeepAliveOnce()
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

std::vector<ipc::MembershipEvent> IpcNodeServiceBase::DrainMembershipEvents()
{
    std::scoped_lock lock(mMutex);
    return mDiscovery.DrainEvents();
}

std::vector<ipc::ProcessDescriptor> IpcNodeServiceBase::Members() const
{
    std::scoped_lock lock(mMutex);
    return mDiscovery.All();
}

std::vector<ipc::ProcessRef> IpcNodeServiceBase::HealthyLinks() const
{
    std::scoped_lock lock(mMutex);
    if (!mLinkManager)
    {
        return {};
    }
    return mLinkManager->GetHealthyLinks();
}

void IpcNodeServiceBase::FlushLinkFrames()
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

bool IpcNodeServiceBase::IsIpcActiveLocked() const
{
    return mRegistered && mIpcReady;
}

std::optional<ipc::ProcessDescriptor> IpcNodeServiceBase::FindDiscoveredMemberLocked(
    const ipc::ServiceType service_type,
    const ipc::InstanceId instance_id,
    const bool exclude_self) const
{
    for (const auto& member : mDiscovery.All())
    {
        if (member.process.process_id.service_type != service_type ||
            member.process.process_id.instance_id != instance_id)
        {
            continue;
        }
        if (exclude_self && mSelf.has_value() && member.process == mSelf->process)
        {
            continue;
        }
        return member;
    }

    return std::nullopt;
}

ipc::Result IpcNodeServiceBase::ConnectDiscoveredMemberLocked(const ipc::ProcessDescriptor& member)
{
    if (!mTransport)
    {
        return ipc::Result::Failure("transport is not initialized");
    }

    return mTransport->Connect(member.listen_endpoint);
}

void IpcNodeServiceBase::RecordAutoConnectFailureLocked(
    const ipc::ProcessDescriptor& member,
    const ipc::Result& result,
    const std::string_view actor_name)
{
    ++mAutoConnectFailureCount;
    mLastAutoConnectFailureTarget = member.process;
    mLastAutoConnectFailureReason = result.message;
    mLastError = result.message;
    spdlog::warn(
        "{} auto-connect failed: service_type={} instance_id={} error={}",
        actor_name,
        member.process.process_id.service_type,
        member.process.process_id.instance_id,
        result.message);
}

void IpcNodeServiceBase::RecordAutoConnectSuccessLocked(
    const ipc::ProcessDescriptor& member,
    const std::string_view actor_name)
{
    mAutoConnectAttempts.insert(MakeProcessKey(member.process.process_id));
    ++mAutoConnectSuccessCount;
    mLastAutoConnectTarget = member.process;
    mLastAutoConnectFailureTarget.reset();
    mLastAutoConnectFailureReason.clear();
    spdlog::info(
        "{} auto-connect: service_type={} instance_id={} host={} port={}",
        actor_name,
        member.process.process_id.service_type,
        member.process.process_id.instance_id,
        member.listen_endpoint.host,
        member.listen_endpoint.port);
}

IpcNodeBaseStatusSnapshot IpcNodeServiceBase::SnapshotBaseStatusLocked() const
{
    IpcNodeBaseStatusSnapshot status;
    if (mSelf.has_value())
    {
        status.has_self = true;
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
    status.discovery_runtime = mDiscovery.RuntimeStats();
    status.member_count = mDiscovery.All().size();
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
    status.last_error = mLastError;
    return status;
}

std::uint64_t IpcNodeServiceBase::MakeProcessKey(const ipc::ProcessId& id)
{
    return (static_cast<std::uint64_t>(id.service_type) << 32) | id.instance_id;
}

void IpcNodeServiceBase::StartAutoConnectLoop()
{
    if (mAutoConnectThread.joinable())
    {
        return;
    }

    mStopAutoConnect = false;
    mAutoConnectThread = std::thread(&IpcNodeServiceBase::AutoConnectLoop, this);
}

void IpcNodeServiceBase::StopAutoConnectLoop()
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

void IpcNodeServiceBase::ReconcileAutoConnectMembers()
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

void IpcNodeServiceBase::StartKeepAliveLoop()
{
    if (mKeepAliveThread.joinable())
    {
        return;
    }
    if (mCore.keepalive_interval_seconds == 0)
    {
        return;
    }

    mStopKeepAlive = false;
    mKeepAliveThread = std::thread(&IpcNodeServiceBase::KeepAliveLoop, this, mCore.keepalive_interval_seconds);
}

void IpcNodeServiceBase::StopKeepAliveLoop()
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

void IpcNodeServiceBase::KeepAliveLoop(const std::uint32_t interval_seconds)
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
                spdlog::info("{} discovery recovered", GetName());
                continue;
            }

            ++mDiscoveryRecoveryFailureCount;
            mLastError = recover_result.message;
            spdlog::warn("{} discovery recovery failed: {}", GetName(), mLastError);
            continue;
        }

        lock.unlock();
        const ipc::Result keepalive_result = mDiscovery.KeepAliveOnce();
        lock.lock();
        if (!keepalive_result.ok)
        {
            ++mKeepAliveFailureCount;
            HandleDiscoveryFailureLocked(keepalive_result.message);
            spdlog::warn("{} discovery keepalive failed: {}", GetName(), mLastError);
        }
    }

    mKeepAliveRunning.store(false);
}

void IpcNodeServiceBase::AutoConnectLoop()
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
            needs_reconcile = ShouldRefreshAutoConnectLocked();
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
            spdlog::warn("{} auto-connect refresh failed: {}", GetName(), refresh_result.message);
            continue;
        }

        ReconcileAutoConnectMembers();
    }
}

void IpcNodeServiceBase::HandleMembershipEvent(const ipc::MembershipEvent& event)
{
    if (event.type == ipc::MembershipEventType::removed)
    {
        std::scoped_lock lock(mMutex);
        mAutoConnectAttempts.erase(MakeProcessKey(event.process.process.process_id));
        return;
    }

    TryAutoConnectMember(event.process);
}

ipc::Result IpcNodeServiceBase::TryRecoverDiscovery()
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

    OnDiscoveryRecovered();
    return ipc::Result::Success();
}

void IpcNodeServiceBase::HandleDiscoveryFailureLocked(const std::string& message)
{
    mRegistered = false;
    mIpcReady = false;
    mLastError = message;
    HandleDiscoveryFailureLockedExtra(message);
}
} // namespace some_server::common
