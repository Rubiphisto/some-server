#include "messenger.h"

namespace ipc
{
SendResult Messenger::SendToProcess(const ProcessId target, const google::protobuf::Message& message) const
{
    Envelope envelope;
    envelope.header.source_process = mSelf;
    envelope.header.target_receiver = ProcessReceiver(target);
    envelope.payload_type_url = PayloadRegistry::TypeUrlFor(message);
    const std::string bytes = message.SerializeAsString();
    envelope.payload_bytes.assign(
        reinterpret_cast<const std::byte*>(bytes.data()),
        reinterpret_cast<const std::byte*>(bytes.data() + bytes.size()));

    std::optional<ReceiverLocation> location;
    if (target == mSelf.process_id)
    {
        location = ReceiverLocation{
            .kind = ReceiverLocationKind::local,
            .processes = {mSelf},
            .version = 1};
    }
    return SendEnvelope(std::move(envelope), std::move(location));
}

SendResult Messenger::SendToReceiver(const ReceiverAddress& target, const google::protobuf::Message& message) const
{
    Envelope envelope;
    envelope.header.source_process = mSelf;
    envelope.header.target_receiver = target;
    envelope.payload_type_url = PayloadRegistry::TypeUrlFor(message);
    const std::string bytes = message.SerializeAsString();
    envelope.payload_bytes.assign(
        reinterpret_cast<const std::byte*>(bytes.data()),
        reinterpret_cast<const std::byte*>(bytes.data() + bytes.size()));

    return SendEnvelope(std::move(envelope), mReceiverDirectory.Resolve(target));
}

SendResult Messenger::Broadcast(const BroadcastScope& scope, const google::protobuf::Message& message) const
{
    Envelope envelope;
    envelope.header.source_process = mSelf;
    envelope.header.semantic = DeliverySemantic::broadcast;
    envelope.broadcast_scope = scope;
    envelope.payload_type_url = PayloadRegistry::TypeUrlFor(message);
    const std::string bytes = message.SerializeAsString();
    envelope.payload_bytes.assign(
        reinterpret_cast<const std::byte*>(bytes.data()),
        reinterpret_cast<const std::byte*>(bytes.data() + bytes.size()));

    return SendEnvelope(std::move(envelope), std::nullopt);
}

ReceiverAddress Messenger::ProcessReceiver(const ProcessId target)
{
    return ReceiverAddress{
        .type = ReceiverType::process,
        .key_hi = target.service_type,
        .key_lo = target.instance_id};
}

SendResult Messenger::SendEnvelope(Envelope envelope, std::optional<ReceiverLocation> receiver_location) const
{
    RoutingContext context;
    context.self = mSelf;
    context.envelope = envelope;
    context.receiver_location = std::move(receiver_location);

    const RoutePlan plan = mRouter.Resolve(context);
    if (plan.kind == RoutePlanKind::local_delivery)
    {
        return DispatchLocal(envelope);
    }

    if (plan.kind == RoutePlanKind::drop)
    {
        return SendResult::Failure("message dropped during route resolution");
    }

    return SendResult::Failure("remote IPC delivery is not implemented yet");
}

DispatchResult Messenger::DispatchLocal(const Envelope& envelope) const
{
    if (!mPayloadRegistry.IsRegistered(envelope.payload_type_url))
    {
        return DispatchResult::Failure("payload type is not registered");
    }

    return mReceiverRegistry.Dispatch(envelope.header.target_receiver, envelope);
}
} // namespace ipc
