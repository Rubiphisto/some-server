#include "framework/ipc/messaging/messenger.h"
#include "framework/ipc/messaging/data_codec.h"
#include "framework/ipc/messaging/payload_registry.h"
#include "framework/ipc/messaging/remote_message_sender.h"
#include "framework/ipc/discovery/membership_view.h"
#include "framework/ipc/link/link_view.h"
#include "framework/ipc/receiver/local_receiver_directory.h"
#include "framework/ipc/receiver/receiver_registry.h"
#include "framework/ipc/routing/relay_first_policy.h"

#include <google/protobuf/wrappers.pb.h>

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class RecordingHost final : public ipc::IReceiverHost
{
public:
    explicit RecordingHost(const ipc::ReceiverType type)
        : mType(type)
    {
    }

    bool CanHandle(const ipc::ReceiverType type) const override
    {
        return type == mType;
    }

    ipc::DispatchResult Dispatch(const ipc::ReceiverAddress& target, const ipc::Envelope& envelope) override
    {
        ++mDispatchCount;
        mLastTarget = target;
        mLastEnvelope = envelope;
        return ipc::DispatchResult::Success();
    }

    int DispatchCount() const
    {
        return mDispatchCount;
    }

    const ipc::ReceiverAddress& LastTarget() const
    {
        return mLastTarget;
    }

    const ipc::Envelope& LastEnvelope() const
    {
        return mLastEnvelope;
    }

private:
    ipc::ReceiverType mType;
    int mDispatchCount = 0;
    ipc::ReceiverAddress mLastTarget;
    ipc::Envelope mLastEnvelope;
};

class StaticMembershipView final : public ipc::IMembershipView
{
public:
    explicit StaticMembershipView(std::vector<ipc::ProcessDescriptor> processes)
        : mProcesses(std::move(processes))
    {
    }

    std::optional<ipc::ProcessDescriptor> Find(const ipc::ProcessId& id) const override
    {
        for (const auto& process : mProcesses)
        {
            if (process.process.process_id == id)
            {
                return process;
            }
        }
        return std::nullopt;
    }

    std::vector<ipc::ProcessDescriptor> FindByService(const ipc::ServiceType type) const override
    {
        std::vector<ipc::ProcessDescriptor> matches;
        for (const auto& process : mProcesses)
        {
            if (process.process.process_id.service_type == type)
            {
                matches.push_back(process);
            }
        }
        return matches;
    }

    std::vector<ipc::ProcessDescriptor> All() const override
    {
        return mProcesses;
    }

private:
    std::vector<ipc::ProcessDescriptor> mProcesses;
};

class StaticLinkView final : public ipc::ILinkView
{
public:
    explicit StaticLinkView(std::vector<ipc::ProcessRef> links)
        : mLinks(std::move(links))
    {
    }

    bool HasHealthyDirectLink(const ipc::ProcessRef& target) const override
    {
        for (const auto& link : mLinks)
        {
            if (link == target)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<ipc::ProcessRef> GetHealthyLinks() const override
    {
        return mLinks;
    }

private:
    std::vector<ipc::ProcessRef> mLinks;
};

class RecordingRemoteSender final : public ipc::IRemoteMessageSender
{
public:
    ipc::SendResult Send(const ipc::ProcessRef& next_hop, const ipc::Envelope& envelope) const override
    {
        ++mSendCount;
        mLastNextHop = next_hop;
        mLastEnvelope = envelope;
        return ipc::SendResult::Success();
    }

    int SendCount() const
    {
        return mSendCount;
    }

    const ipc::ProcessRef& LastNextHop() const
    {
        return mLastNextHop;
    }

    const ipc::Envelope& LastEnvelope() const
    {
        return mLastEnvelope;
    }

private:
    mutable int mSendCount = 0;
    mutable ipc::ProcessRef mLastNextHop;
    mutable ipc::Envelope mLastEnvelope;
};

void TestReceiverDirectoryLifecycle()
{
    ipc::LocalReceiverDirectory directory;
    const ipc::ReceiverAddress receiver{ipc::ReceiverType::player, 1001, 0};
    const ipc::ProcessRef owner_a{{10, 1}, 101};
    const ipc::ProcessRef owner_b{{10, 2}, 202};

    Require(directory.Bind(receiver, owner_a).ok, "bind receiver");
    ipc::ReceiverLocation location = directory.Resolve(receiver);
    Require(location.kind == ipc::ReceiverLocationKind::single_process, "bind resolves to single process");
    Require(location.processes.size() == 1 && location.processes.front() == owner_a, "bind owner");
    Require(location.version == 1, "bind version");

    Require(!directory.Bind(receiver, owner_b).ok, "second owner should be rejected");
    Require(directory.Rebind(receiver, owner_a, owner_b).ok, "rebind receiver");
    location = directory.Resolve(receiver);
    Require(location.processes.front() == owner_b, "rebind owner");
    Require(location.version == 2, "rebind version");

    Require(!directory.Invalidate(receiver, owner_a, 2).ok, "stale owner invalidate should fail");
    Require(directory.Invalidate(receiver, owner_b, 2).ok, "invalidate current owner");
    location = directory.Resolve(receiver);
    Require(location.kind == ipc::ReceiverLocationKind::unresolved, "receiver becomes unresolved");
}

void TestMessengerLocalDispatch()
{
    const ipc::ProcessRef self{{10, 1}, 101};
    const ipc::ReceiverAddress receiver{ipc::ReceiverType::service, 10, 9001};

    ipc::LocalReceiverDirectory directory;
    Require(directory.Bind(receiver, self).ok, "bind local service receiver");

    RecordingHost host(ipc::ReceiverType::service);
    ipc::ReceiverRegistry receiver_registry;
    Require(receiver_registry.Register(host, ipc::ReceiverType::service).ok, "register service host");

    google::protobuf::StringValue payload;
    payload.set_value("hello");

    ipc::PayloadRegistry payload_registry;
    Require(payload_registry.Register(payload).ok, "register payload type");

    ipc::RelayFirstPolicy policy(99);
    ipc::Router router(policy);
    ipc::Messenger messenger(self, router, directory, receiver_registry, payload_registry);

    const ipc::SendResult send_result = messenger.SendToReceiver(receiver, payload);
    Require(send_result.ok, "local send to receiver");
    Require(host.DispatchCount() == 1, "host dispatch count");
    Require(host.LastTarget() == receiver, "host target");
    Require(host.LastEnvelope().payload_type_url == ipc::PayloadRegistry::TypeUrlFor(payload), "payload type url");
    Require(!host.LastEnvelope().payload_bytes.empty(), "payload bytes");
}

void TestMessengerRejectsUnknownPayload()
{
    const ipc::ProcessRef self{{10, 1}, 101};
    const ipc::ReceiverAddress receiver{ipc::ReceiverType::service, 10, 42};

    ipc::LocalReceiverDirectory directory;
    Require(directory.Bind(receiver, self).ok, "bind local service receiver");

    RecordingHost host(ipc::ReceiverType::service);
    ipc::ReceiverRegistry receiver_registry;
    Require(receiver_registry.Register(host, ipc::ReceiverType::service).ok, "register service host");

    ipc::PayloadRegistry payload_registry;
    ipc::RelayFirstPolicy policy(99);
    ipc::Router router(policy);
    ipc::Messenger messenger(self, router, directory, receiver_registry, payload_registry);

    google::protobuf::StringValue payload;
    payload.set_value("hello");

    const ipc::SendResult send_result = messenger.SendToReceiver(receiver, payload);
    Require(!send_result.ok, "unknown payload should be rejected");
    Require(host.DispatchCount() == 0, "host should not receive unknown payload");
}

void TestMessengerDispatchesLocalPlayerReceiver()
{
    const ipc::ProcessRef self{{10, 1}, 101};
    const ipc::ReceiverAddress receiver{ipc::ReceiverType::player, 1001, 0};

    ipc::LocalReceiverDirectory directory;
    Require(directory.Bind(receiver, self).ok, "bind local player receiver");

    RecordingHost host(ipc::ReceiverType::player);
    ipc::ReceiverRegistry receiver_registry;
    Require(receiver_registry.Register(host, ipc::ReceiverType::player).ok, "register player host");

    google::protobuf::StringValue payload;
    payload.set_value("player-local");

    ipc::PayloadRegistry payload_registry;
    Require(payload_registry.Register(payload).ok, "register payload type");

    ipc::RelayFirstPolicy policy(99);
    ipc::Router router(policy);
    ipc::Messenger messenger(self, router, directory, receiver_registry, payload_registry);

    const ipc::SendResult send_result = messenger.SendToReceiver(receiver, payload);
    Require(send_result.ok, "local send to player receiver");
    Require(host.DispatchCount() == 1, "player host dispatch count");
    Require(host.LastTarget() == receiver, "player host target");
}

void TestMessengerSendsRemoteProcessMessage()
{
    const ipc::ProcessRef self{{10, 1}, 101};
    const ipc::ProcessRef remote{{10, 2}, 202};

    ipc::LocalReceiverDirectory directory;
    RecordingHost host(ipc::ReceiverType::service);
    ipc::ReceiverRegistry receiver_registry;
    Require(receiver_registry.Register(host, ipc::ReceiverType::service).ok, "register service host");

    google::protobuf::StringValue payload;
    payload.set_value("remote");

    ipc::PayloadRegistry payload_registry;
    Require(payload_registry.Register(payload).ok, "register payload type");

    StaticMembershipView membership({
        ipc::ProcessDescriptor{
            .process = remote,
            .service_name = "game",
            .listen_endpoint = {"127.0.0.1", 9101},
            .protocol_version = 1}});
    StaticLinkView links({remote});
    RecordingRemoteSender sender;

    ipc::RelayFirstPolicy policy(99);
    ipc::Router router(policy);
    ipc::Messenger messenger(self, router, directory, receiver_registry, payload_registry, &membership, &links, &sender);

    const ipc::SendResult send_result = messenger.SendToProcess(remote.process_id, payload);
    Require(send_result.ok, "remote send to process");
    Require(sender.SendCount() == 1, "remote sender count");
    Require(sender.LastNextHop() == remote, "remote next hop");
    Require(sender.LastEnvelope().header.target_receiver.type == ipc::ReceiverType::process, "process receiver type");
    Require(!sender.LastEnvelope().payload_bytes.empty(), "remote payload bytes");
}

void TestMessengerSendsRemotePlayerMessage()
{
    const ipc::ProcessRef self{{10, 1}, 101};
    const ipc::ProcessRef remote{{10, 2}, 202};
    const ipc::ReceiverAddress receiver{ipc::ReceiverType::player, 1001, 0};

    ipc::LocalReceiverDirectory directory;
    Require(directory.Bind(receiver, remote).ok, "bind remote player receiver");

    RecordingHost host(ipc::ReceiverType::player);
    ipc::ReceiverRegistry receiver_registry;
    Require(receiver_registry.Register(host, ipc::ReceiverType::player).ok, "register player host");

    google::protobuf::StringValue payload;
    payload.set_value("player-remote");

    ipc::PayloadRegistry payload_registry;
    Require(payload_registry.Register(payload).ok, "register payload type");

    StaticMembershipView membership({
        ipc::ProcessDescriptor{
            .process = remote,
            .service_name = "game",
            .listen_endpoint = {"127.0.0.1", 9101},
            .protocol_version = 1}});
    StaticLinkView links({remote});
    RecordingRemoteSender sender;

    ipc::RelayFirstPolicy policy(99);
    ipc::Router router(policy);
    ipc::Messenger messenger(self, router, directory, receiver_registry, payload_registry, &membership, &links, &sender);

    const ipc::SendResult send_result = messenger.SendToReceiver(receiver, payload);
    Require(send_result.ok, "remote send to player receiver");
    Require(sender.SendCount() == 1, "remote player sender count");
    Require(sender.LastNextHop() == remote, "remote player next hop");
    Require(sender.LastEnvelope().header.target_receiver == receiver, "remote player target");
    Require(sender.LastEnvelope().header.resolved_target_process.has_value(), "remote player resolved owner");
    Require(*sender.LastEnvelope().header.resolved_target_process == remote, "remote player resolved owner value");
    Require(host.DispatchCount() == 0, "source player host should not dispatch remote message");
}

void TestMessengerForwardsIncomingProcessMessage()
{
    const ipc::ProcessRef self{{99, 1}, 901};
    const ipc::ProcessRef remote{{10, 2}, 202};
    const ipc::ReceiverAddress process_receiver{
        .type = ipc::ReceiverType::process,
        .key_hi = remote.process_id.service_type,
        .key_lo = remote.process_id.instance_id};

    ipc::LocalReceiverDirectory directory;
    RecordingHost host(ipc::ReceiverType::service);
    ipc::ReceiverRegistry receiver_registry;
    Require(receiver_registry.Register(host, ipc::ReceiverType::service).ok, "register service host");

    google::protobuf::StringValue payload;
    payload.set_value("forwarded");

    ipc::PayloadRegistry payload_registry;
    Require(payload_registry.Register(payload).ok, "register payload type");

    StaticMembershipView membership({
        ipc::ProcessDescriptor{
            .process = remote,
            .service_name = "game",
            .listen_endpoint = {"127.0.0.1", 9101},
            .protocol_version = 1}});
    StaticLinkView links({remote});
    RecordingRemoteSender sender;

    ipc::RelayFirstPolicy policy(99);
    ipc::Router router(policy);
    ipc::Messenger messenger(self, router, directory, receiver_registry, payload_registry, &membership, &links, &sender);

    ipc::Envelope envelope;
    envelope.header.source_process = {{10, 1}, 101};
    envelope.header.target_receiver = process_receiver;
    envelope.payload_type_url = ipc::PayloadRegistry::TypeUrlFor(payload);
    const std::string bytes = payload.SerializeAsString();
    envelope.payload_bytes.assign(
        reinterpret_cast<const std::byte*>(bytes.data()),
        reinterpret_cast<const std::byte*>(bytes.data() + bytes.size()));

    const std::vector<std::byte> encoded = ipc::EncodeDataEnvelope(envelope);
    Require(!encoded.empty(), "encode forwarded envelope");

    ipc::RawFrame frame;
    frame.connection_id = 1;
    frame.header.kind = ipc::FrameKind::data;
    frame.header.length = static_cast<std::uint32_t>(encoded.size());
    frame.payload = encoded;

    const ipc::Result handle_result = messenger.HandleIncomingFrame(frame);
    Require(handle_result.ok, "forward incoming process message");
    Require(sender.SendCount() == 1, "forwarded sender count");
    Require(sender.LastNextHop() == remote, "forward next hop");
    Require(sender.LastEnvelope().header.target_receiver.type == ipc::ReceiverType::process, "forward process receiver type");
    Require(host.DispatchCount() == 0, "relay should not local-dispatch forwarded process message");
}

void TestMessengerForwardsIncomingPlayerMessage()
{
    const ipc::ProcessRef self{{99, 1}, 901};
    const ipc::ProcessRef remote{{10, 2}, 202};
    const ipc::ReceiverAddress player_receiver{
        .type = ipc::ReceiverType::player,
        .key_hi = 1001,
        .key_lo = 0};

    ipc::LocalReceiverDirectory directory;
    RecordingHost host(ipc::ReceiverType::player);
    ipc::ReceiverRegistry receiver_registry;
    Require(receiver_registry.Register(host, ipc::ReceiverType::player).ok, "register player host");

    google::protobuf::StringValue payload;
    payload.set_value("forwarded-player");

    ipc::PayloadRegistry payload_registry;
    Require(payload_registry.Register(payload).ok, "register payload type");

    StaticMembershipView membership({
        ipc::ProcessDescriptor{
            .process = remote,
            .service_name = "game",
            .listen_endpoint = {"127.0.0.1", 9101},
            .protocol_version = 1}});
    StaticLinkView links({remote});
    RecordingRemoteSender sender;

    ipc::RelayFirstPolicy policy(99);
    ipc::Router router(policy);
    ipc::Messenger messenger(self, router, directory, receiver_registry, payload_registry, &membership, &links, &sender);

    ipc::Envelope envelope;
    envelope.header.source_process = {{10, 1}, 101};
    envelope.header.target_receiver = player_receiver;
    envelope.header.resolved_target_process = remote;
    envelope.payload_type_url = ipc::PayloadRegistry::TypeUrlFor(payload);
    const std::string bytes = payload.SerializeAsString();
    envelope.payload_bytes.assign(
        reinterpret_cast<const std::byte*>(bytes.data()),
        reinterpret_cast<const std::byte*>(bytes.data() + bytes.size()));

    const std::vector<std::byte> encoded = ipc::EncodeDataEnvelope(envelope);
    Require(!encoded.empty(), "encode forwarded player envelope");

    ipc::RawFrame frame;
    frame.connection_id = 1;
    frame.header.kind = ipc::FrameKind::data;
    frame.header.length = static_cast<std::uint32_t>(encoded.size());
    frame.payload = encoded;

    const ipc::Result handle_result = messenger.HandleIncomingFrame(frame);
    Require(handle_result.ok, "forward incoming player message");
    Require(sender.SendCount() == 1, "forwarded player sender count");
    Require(sender.LastNextHop() == remote, "forward player next hop");
    Require(sender.LastEnvelope().header.target_receiver == player_receiver, "forward player target");
    Require(sender.LastEnvelope().header.resolved_target_process.has_value(), "forward player resolved owner");
    Require(host.DispatchCount() == 0, "relay should not local-dispatch forwarded player message");
}
} // namespace

int main()
{
    try
    {
        TestReceiverDirectoryLifecycle();
        TestMessengerLocalDispatch();
        TestMessengerDispatchesLocalPlayerReceiver();
        TestMessengerRejectsUnknownPayload();
        TestMessengerSendsRemoteProcessMessage();
        TestMessengerSendsRemotePlayerMessage();
        TestMessengerForwardsIncomingProcessMessage();
        TestMessengerForwardsIncomingPlayerMessage();
        std::cout << "ipc_receiver_messaging_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "ipc_receiver_messaging_test: " << ex.what() << std::endl;
        return 1;
    }
}
