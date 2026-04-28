#pragma once

#include "../application.h"
#include "../../framework/application/service_base.h"
#include "../../framework/ipc/discovery/etcd_discovery.h"
#include "../../framework/ipc/link/link_manager.h"
#include "../../framework/ipc/messaging/messenger.h"
#include "../../framework/ipc/messaging/payload_registry.h"
#include "../../framework/ipc/messaging/transport_message_sender.h"
#include "../../framework/ipc/receiver/local_receiver_directory.h"
#include "../../framework/ipc/receiver/receiver_registry.h"
#include "../../framework/ipc/routing/relay_first_policy.h"
#include "../../framework/ipc/routing/router.h"
#include "../../framework/ipc/transport/tcp_transport.h"
#include "player_receiver_host.h"
#include "process_receiver_host.h"
#include "service_receiver_host.h"

#include <atomic>
#include <condition_variable>
#include <google/protobuf/wrappers.pb.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

struct GameIpcClientStatus
{
    ipc::ProcessDescriptor self;
    bool transport_ready = false;
    bool registered = false;
    bool ipc_ready = false;
    bool keepalive_running = false;
    bool watch_running = false;
    std::size_t member_count = 0;
    std::uint64_t process_dispatch_count = 0;
    std::string last_process_payload_type;
    std::uint64_t player_dispatch_count = 0;
    std::uint64_t last_player_id = 0;
    std::string last_player_payload_type;
    std::uint64_t local_service_dispatch_count = 0;
    std::string last_payload_type;
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
    std::vector<ipc::ProcessRef> HealthyLinks() const;
    ipc::Result ConnectToProcess(ipc::InstanceId instance_id);
    ipc::Result BindLocalPlayer(std::uint64_t player_id);
    ipc::Result BindRemotePlayer(std::uint64_t player_id, ipc::InstanceId instance_id);
    ipc::SendResult SendLocalServiceMessage(const std::string& value);
    ipc::SendResult SendProcessMessage(ipc::InstanceId instance_id, const std::string& value);
    ipc::SendResult SendPlayerMessage(std::uint64_t player_id, const std::string& value);
    ipc::SendResult BroadcastServiceMessage(const std::string& value, bool include_local);

private:
    ipc::ProcessDescriptor BuildSelfDescriptor() const;
    ipc::ReceiverAddress LocalServiceReceiverAddress() const;
    static ipc::ReceiverAddress PlayerReceiverAddress(std::uint64_t player_id);
    void FlushLinkFrames();
    void StartKeepAliveLoop();
    void StopKeepAliveLoop();
    void KeepAliveLoop(std::uint32_t interval_seconds);
    void StartAutoConnectLoop();
    void StopAutoConnectLoop();
    void AutoConnectLoop();
    void ReconcileAutoConnectMembers();
    void HandleMembershipEvent(const ipc::MembershipEvent& event);
    void HandleDiscoveryFailureLocked(const std::string& message);
    void TryAutoConnectMember(const ipc::ProcessDescriptor& member);
    bool HasRelayMemberInDiscoveryLocked() const;
    bool HasHealthyRelayLink() const;
    bool IsIpcActiveLocked() const;
    static std::uint64_t MakeProcessKey(const ipc::ProcessId& id);

    GameConfiguration mConfiguration;
    ipc::ServiceType mGameServiceType = 0;
    ipc::RelayFirstPolicy mRoutingPolicy;
    ipc::Router mRouter;
    std::unique_ptr<ipc::TcpTransport> mTransport;
    std::unique_ptr<ipc::LinkManager> mLinkManager;
    ipc::EtcdDiscovery mDiscovery;
    ipc::LocalReceiverDirectory mReceiverDirectory;
    ipc::ReceiverRegistry mReceiverRegistry;
    ipc::PayloadRegistry mPayloadRegistry;
    std::unique_ptr<ProcessReceiverHost> mProcessReceiverHost;
    PlayerReceiverHost mPlayerReceiverHost;
    ServiceReceiverHost mServiceReceiverHost;
    std::unique_ptr<ipc::TransportMessageSender> mTransportMessageSender;
    std::unique_ptr<ipc::Messenger> mMessenger;
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
    std::condition_variable mAutoConnectWakeup;
    std::thread mAutoConnectThread;
    bool mStopAutoConnect = false;
    std::unordered_set<std::uint64_t> mAutoConnectAttempts;
};
