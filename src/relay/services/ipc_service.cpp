#include "ipc_service.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace
{
constexpr std::int32_t kRelayIpcBatch = 100;
constexpr ipc::ServiceType kRelayServiceType = 99;
}

RelayIpcService::RelayIpcService(const RelayConfiguration& configuration, ipc::ServiceType relay_service_type)
    : ServiceBase("relay_ipc", kRelayIpcBatch)
    , mConfiguration(configuration)
    , mRelayServiceType(relay_service_type)
    , mRoutingPolicy(kRelayServiceType)
    , mRouter(mRoutingPolicy)
    , mDiscovery(ipc::EtcdDiscoveryOptions{
          .etcdctl_path = "etcdctl",
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
                (void)mMessenger->HandleIncomingFrame(frame);
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
    StartKeepAliveLoop();

    return LifecycleTask::Completed();
}

LifecycleTask RelayIpcService::Stop()
{
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
    StopKeepAliveLoop();
    std::scoped_lock lock(mMutex);
    mMessenger.reset();
    mTransportMessageSender.reset();
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
    status.keepalive_running = mKeepAliveRunning.load();
    status.member_count = mDiscovery.All().size();
    status.last_error = mLastError;
    return status;
}

ipc::Result RelayIpcService::RefreshDiscovery()
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

ipc::Result RelayIpcService::KeepAliveOnce()
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
            continue;
        }

        const ipc::Result keepalive_result = mDiscovery.KeepAliveOnce();
        if (!keepalive_result.ok)
        {
            mLastError = keepalive_result.message;
            spdlog::warn("relay ipc discovery keepalive failed: {}", mLastError);
        }
    }

    mKeepAliveRunning.store(false);
}
