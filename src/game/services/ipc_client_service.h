#pragma once

#include "../application.h"
#include "../../framework/application/service_base.h"
#include "../../framework/ipc/discovery/etcd_discovery.h"
#include "../../framework/ipc/link/link_manager.h"
#include "../../framework/ipc/transport/tcp_transport.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct GameIpcClientStatus
{
    ipc::ProcessDescriptor self;
    bool registered = false;
    std::size_t member_count = 0;
    std::string last_error;
};

class GameIpcClientService final : public ServiceBase
{
public:
    GameIpcClientService(const GameConfiguration& configuration, ipc::ServiceType game_service_type);

    LifecycleTask Load() override;
    LifecycleTask Start() override;
    LifecycleTask Stop() override;
    LifecycleTask Unload() override;

    GameIpcClientStatus Snapshot() const;
    ipc::Result RefreshDiscovery();
    ipc::Result KeepAliveOnce();
    std::vector<ipc::MembershipEvent> DrainMembershipEvents();
    std::vector<ipc::ProcessDescriptor> Members() const;

private:
    ipc::ProcessDescriptor BuildSelfDescriptor() const;

    GameConfiguration mConfiguration;
    ipc::ServiceType mGameServiceType = 0;
    std::unique_ptr<ipc::TcpTransport> mTransport;
    std::unique_ptr<ipc::LinkManager> mLinkManager;
    ipc::EtcdDiscovery mDiscovery;
    std::optional<ipc::ProcessDescriptor> mSelf;
    bool mRegistered = false;
    std::string mLastError;
};
