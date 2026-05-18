#pragma once

#include "../application.h"
#include "../../common/protocol/protobuf_envelope_dispatcher.h"
#include "../../common/ipc/ipc_node_service_base.h"
#include "process_receiver_host.h"
#include "service_receiver_host.h"

#include <functional>
#include <utility>
#include <memory>
#include <string>

struct GateIpcStatus
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
    std::uint64_t local_service_dispatch_count = 0;
    std::string last_payload_type;
    std::string last_error;
};

class GateIpcService final : public some_server::common::IpcNodeServiceBase
{
public:
    GateIpcService(const GateConfiguration& configuration, ipc::ServiceType gate_service_type);

    LifecycleTask Load() override;
    LifecycleTask Start() override;
    LifecycleTask Stop() override;
    LifecycleTask Unload() override;

    GateIpcStatus Snapshot() const;
    ipc::SendResult SendProcessPayload(ipc::ProcessId target, const google::protobuf::Message& message);

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
    void RecordSendRejectLocked(const std::string& reason);

    GateConfiguration mConfiguration;
    ipc::ServiceType mGateServiceType = 0;
    std::unique_ptr<ProcessReceiverHost> mProcessReceiverHost;
    ServiceReceiverHost mServiceReceiverHost;
    common::protocol::ProtobufEnvelopeDispatcher<ipc::ReceiverAddress> mProcessDispatcher;
    std::uint64_t mSendRejectCount = 0;
    std::string mLastSendRejectReason;
};
