#pragma once

#include "../application.h"
#include "../../common/protocol/protobuf_envelope_dispatcher.h"
#include "../../common/ipc/ipc_node_service_base.h"
#include "player_receiver_host.h"
#include "process_receiver_host.h"
#include "service_receiver_host.h"

#include <atomic>
#include <functional>
#include <google/protobuf/wrappers.pb.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct GameIpcClientStatus
{
    ipc::ProcessDescriptor self;
    bool transport_ready = false;
    bool registered = false;
    bool ipc_ready = false;
    bool membership_degraded = false;
    bool keepalive_running = false;
    bool watch_running = false;
    std::uint64_t keepalive_failure_count = 0;
    std::uint64_t discovery_recovery_success_count = 0;
    std::uint64_t discovery_recovery_failure_count = 0;
    std::uint64_t send_reject_count = 0;
    std::string last_send_reject_reason;
    ipc::EtcdDiscoveryRuntimeStats discovery_runtime;
    std::size_t member_count = 0;
    bool relay_member_visible = false;
    bool healthy_relay_link = false;
    std::size_t auto_connect_targets = 0;
    std::uint64_t auto_connect_success_count = 0;
    std::uint64_t auto_connect_failure_count = 0;
    bool has_last_auto_connect_target = false;
    ipc::ProcessRef last_auto_connect_target;
    bool has_last_auto_connect_failure_target = false;
    ipc::ProcessRef last_auto_connect_failure_target;
    std::string last_auto_connect_failure_reason;
    std::uint64_t process_dispatch_count = 0;
    std::string last_process_payload_type;
    std::uint64_t player_dispatch_count = 0;
    std::uint64_t last_player_id = 0;
    std::string last_player_payload_type;
    std::uint64_t local_service_dispatch_count = 0;
    std::string last_payload_type;
    std::string last_error;
};

struct GameLocalReceiverSnapshot
{
    ipc::ProcessRef process_receiver;
    ipc::ReceiverAddress service_receiver;
    std::vector<std::uint64_t> local_player_ids;
};

class GameIpcClientService final : public some_server::common::IpcNodeServiceBase
{
public:
    GameIpcClientService(const GameConfiguration& configuration, ipc::ServiceType game_service_type);

    LifecycleTask Load() override;
    LifecycleTask Start() override;
    LifecycleTask Stop() override;
    LifecycleTask Unload() override;

    using IpcNodeServiceBase::DrainMembershipEvents;
    using IpcNodeServiceBase::HealthyLinks;
    using IpcNodeServiceBase::KeepAliveOnce;
    using IpcNodeServiceBase::Members;
    using IpcNodeServiceBase::RefreshDiscovery;

    GameIpcClientStatus Snapshot() const;
    GameLocalReceiverSnapshot LocalReceivers() const;
    ipc::Result ConnectToProcess(ipc::InstanceId instance_id);
    ipc::Result BindLocalPlayer(std::uint64_t player_id);
    ipc::Result UnbindLocalPlayer(std::uint64_t player_id);
    ipc::Result BindRemotePlayer(std::uint64_t player_id, ipc::InstanceId instance_id);
    ipc::SendResult SendProcessPayload(ipc::ProcessId target, const google::protobuf::Message& message);
    ipc::SendResult SendLocalServiceMessage(const std::string& value);
    ipc::SendResult SendProcessMessage(ipc::InstanceId instance_id, const std::string& value);
    ipc::SendResult SendPlayerMessage(std::uint64_t player_id, const std::string& value);
    ipc::SendResult BroadcastServiceMessage(const std::string& value, bool include_local);

    template <typename Message, typename HandlerClass>
    void RegisterProcessHandler(
        HandlerClass* instance,
        ipc::DispatchResult (HandlerClass::*handler)(const ipc::Envelope&, const Message&))
    {
        mProcessDispatcher.Register<Message>(instance, handler);
    }

private:
    ipc::ProcessDescriptor BuildSelfDescriptor() const override;
    ipc::Result SetupRoleComponentsLocked() override;
    void TeardownRoleComponentsLocked() override;
    void HandleIncomingDataFrameLocked(const ipc::RawFrame& frame) override;
    void HandleDiscoveryFailureLockedExtra(const std::string& message) override;
    void OnDiscoveryRecovered() override;
    bool ShouldRefreshAutoConnectLocked() const override;
    void TryAutoConnectMember(const ipc::ProcessDescriptor& member) override;

    ipc::ReceiverAddress LocalServiceReceiverAddress() const;
    static ipc::ReceiverAddress PlayerReceiverAddress(std::uint64_t player_id);
    void RecordSendRejectLocked(const std::string& reason);
    bool HasRelayMemberInDiscoveryLocked() const;
    bool HasHealthyRelayLink() const;

    GameConfiguration mConfiguration;
    ipc::ServiceType mGameServiceType = 0;
    std::unique_ptr<ProcessReceiverHost> mProcessReceiverHost;
    PlayerReceiverHost mPlayerReceiverHost;
    ServiceReceiverHost mServiceReceiverHost;
    common::protocol::ProtobufEnvelopeDispatcher<ipc::ReceiverAddress> mProcessDispatcher;
    std::uint64_t mSendRejectCount = 0;
    std::string mLastSendRejectReason;
};
