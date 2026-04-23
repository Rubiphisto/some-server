#include "ipc_service.h"

#include <spdlog/spdlog.h>

namespace
{
constexpr std::int32_t kRelayIpcBatch = 100;
}

RelayIpcService::RelayIpcService(const RelayConfiguration& configuration, ipc::ServiceType relay_service_type)
    : ServiceBase("relay_ipc", kRelayIpcBatch)
    , mConfiguration(configuration)
    , mRelayServiceType(relay_service_type)
    , mDiscovery(ipc::EtcdDiscoveryOptions{
          .etcdctl_path = "etcdctl",
          .endpoints = configuration.discovery.endpoints,
          .prefix = configuration.discovery.prefix,
          .lease_ttl_seconds = configuration.discovery.lease_ttl_seconds})
{
}

LifecycleTask RelayIpcService::Load()
{
    mSelf = BuildSelfDescriptor();
    mRegistered = false;
    mLastError.clear();
    return LifecycleTask::Completed();
}

LifecycleTask RelayIpcService::Start()
{
    if (!mSelf.has_value())
    {
        mLastError = "self descriptor is not initialized";
        return LifecycleTask::Completed();
    }

    if (const ipc::Result register_result = mDiscovery.RegisterSelf(*mSelf); !register_result.ok)
    {
        mRegistered = false;
        mLastError = register_result.message;
        spdlog::warn("relay ipc discovery register failed: {}", mLastError);
        return LifecycleTask::Completed();
    }

    mRegistered = true;
    mLastError.clear();

    if (const ipc::Result refresh_result = mDiscovery.RefreshSnapshot(); !refresh_result.ok)
    {
        mLastError = refresh_result.message;
        spdlog::warn("relay ipc discovery refresh failed: {}", mLastError);
    }

    return LifecycleTask::Completed();
}

LifecycleTask RelayIpcService::Stop()
{
    if (mRegistered && mSelf.has_value())
    {
        if (const ipc::Result remove_result = mDiscovery.Remove(mSelf->process.process_id); !remove_result.ok)
        {
            mLastError = remove_result.message;
            spdlog::warn("relay ipc discovery remove failed: {}", mLastError);
        }
    }

    mRegistered = false;
    return LifecycleTask::Completed();
}

LifecycleTask RelayIpcService::Unload()
{
    mSelf.reset();
    return LifecycleTask::Completed();
}

RelayIpcStatus RelayIpcService::Snapshot() const
{
    RelayIpcStatus status;
    if (mSelf.has_value())
    {
        status.self = *mSelf;
    }
    status.registered = mRegistered;
    status.member_count = mDiscovery.All().size();
    status.last_error = mLastError;
    return status;
}

ipc::Result RelayIpcService::RefreshDiscovery()
{
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
    return mDiscovery.DrainEvents();
}

std::vector<ipc::ProcessDescriptor> RelayIpcService::Members() const
{
    return mDiscovery.All();
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
