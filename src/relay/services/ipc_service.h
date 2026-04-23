#pragma once

#include "../application.h"
#include "../../framework/application/service_base.h"
#include "../../framework/ipc/discovery/etcd_discovery.h"

#include <optional>
#include <string>

struct RelayIpcStatus
{
    ipc::ProcessDescriptor self;
    bool registered = false;
    std::size_t member_count = 0;
    std::string last_error;
};

class RelayIpcService final : public ServiceBase
{
public:
    RelayIpcService(const RelayConfiguration& configuration, ipc::ServiceType relay_service_type);

    LifecycleTask Load() override;
    LifecycleTask Start() override;
    LifecycleTask Stop() override;
    LifecycleTask Unload() override;

    RelayIpcStatus Snapshot() const;
    ipc::Result RefreshDiscovery();
    ipc::Result KeepAliveOnce();
    std::vector<ipc::MembershipEvent> DrainMembershipEvents();
    std::vector<ipc::ProcessDescriptor> Members() const;

private:
    ipc::ProcessDescriptor BuildSelfDescriptor() const;

    RelayConfiguration mConfiguration;
    ipc::ServiceType mRelayServiceType = 0;
    ipc::EtcdDiscovery mDiscovery;
    std::optional<ipc::ProcessDescriptor> mSelf;
    bool mRegistered = false;
    std::string mLastError;
};
