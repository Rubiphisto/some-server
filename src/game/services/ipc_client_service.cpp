#include "ipc_client_service.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace
{
constexpr std::int32_t kGameIpcBatch = 100;
}

GameIpcClientService::GameIpcClientService(const GameConfiguration& configuration, ipc::ServiceType game_service_type)
    : ServiceBase("game_ipc_client", kGameIpcBatch)
    , mConfiguration(configuration)
    , mGameServiceType(game_service_type)
    , mDiscovery(ipc::EtcdDiscoveryOptions{
          .etcdctl_path = "etcdctl",
          .endpoints = configuration.discovery.endpoints,
          .prefix = configuration.discovery.prefix,
          .lease_ttl_seconds = configuration.discovery.lease_ttl_seconds})
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
            }
        });
    mTransport->SetFrameHandler(
        [this](const ipc::RawFrame& frame) {
            if (mLinkManager)
            {
                (void)mLinkManager->OnFrame(frame);
            }
        });
    mRegistered = false;
    mTransportReady = false;
    mIpcReady = false;
    mLastError.clear();
    return LifecycleTask::Completed();
}

LifecycleTask GameIpcClientService::Start()
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
    StartKeepAliveLoop();

    return LifecycleTask::Completed();
}

LifecycleTask GameIpcClientService::Stop()
{
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
    StopKeepAliveLoop();
    std::scoped_lock lock(mMutex);
    mLinkManager.reset();
    mTransport.reset();
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
    status.keepalive_running = mKeepAliveRunning.load();
    status.member_count = mDiscovery.All().size();
    status.last_error = mLastError;
    return status;
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
            mLastError = keepalive_result.message;
            spdlog::warn("game ipc discovery keepalive failed: {}", mLastError);
        }
    }

    mKeepAliveRunning.store(false);
}
