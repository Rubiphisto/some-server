#pragma once

#include "../application.h"
#include "../../framework/application/service_base.h"
#include "../../framework/ipc/discovery/etcd_discovery.h"
#include "../../framework/ipc/link/link_manager.h"
#include "../../framework/ipc/transport/tcp_transport.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

struct RelayIpcStatus
{
    ipc::ProcessDescriptor self;
    bool transport_ready = false;
    bool registered = false;
    bool ipc_ready = false;
    bool keepalive_running = false;
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
    void StartKeepAliveLoop();
    void StopKeepAliveLoop();
    void KeepAliveLoop(std::uint32_t interval_seconds);

    RelayConfiguration mConfiguration;
    ipc::ServiceType mRelayServiceType = 0;
    std::unique_ptr<ipc::TcpTransport> mTransport;
    std::unique_ptr<ipc::LinkManager> mLinkManager;
    ipc::EtcdDiscovery mDiscovery;
    std::optional<ipc::ProcessDescriptor> mSelf;
    bool mRegistered = false;
    bool mTransportReady = false;
    bool mIpcReady = false;
    std::string mLastError;
    mutable std::mutex mMutex;
    std::condition_variable mKeepAliveWakeup;
    std::thread mKeepAliveThread;
    bool mStopKeepAlive = false;
    std::atomic<bool> mKeepAliveRunning = false;
};
