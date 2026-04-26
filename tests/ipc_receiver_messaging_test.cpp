#include "framework/ipc/messaging/messenger.h"
#include "framework/ipc/messaging/payload_registry.h"
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
} // namespace

int main()
{
    try
    {
        TestReceiverDirectoryLifecycle();
        TestMessengerLocalDispatch();
        TestMessengerRejectsUnknownPayload();
        std::cout << "ipc_receiver_messaging_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "ipc_receiver_messaging_test: " << ex.what() << std::endl;
        return 1;
    }
}
