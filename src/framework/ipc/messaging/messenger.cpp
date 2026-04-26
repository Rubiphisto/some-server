#include "messenger.h"

#include "data_codec.h"

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

SendResult Messenger::BroadcastToReceiver(
    const ReceiverAddress& target,
    BroadcastScope scope,
    const google::protobuf::Message& message) const
{
    if (target.type != ReceiverType::service)
    {
        return SendResult::Failure("broadcast is not supported for this receiver type");
    }
    if (scope.service_type.has_value() && *scope.service_type != static_cast<ServiceType>(target.key_hi))
    {
        return SendResult::Failure("broadcast scope service_type does not match target receiver");
    }

    scope.service_type = static_cast<ServiceType>(target.key_hi);

    Envelope envelope;
    envelope.header.source_process = mSelf;
    envelope.header.semantic = DeliverySemantic::broadcast;
    envelope.header.target_receiver = target;
    envelope.broadcast_scope = std::move(scope);
    envelope.payload_type_url = PayloadRegistry::TypeUrlFor(message);
    const std::string bytes = message.SerializeAsString();
    envelope.payload_bytes.assign(
        reinterpret_cast<const std::byte*>(bytes.data()),
        reinterpret_cast<const std::byte*>(bytes.data() + bytes.size()));

    return SendEnvelope(std::move(envelope), std::nullopt);
}

SendResult Messenger::BroadcastToService(
    const ServiceType service_type,
    const BroadcastScope& scope,
    const google::protobuf::Message& message) const
{
    return BroadcastToReceiver(
        ReceiverAddress{
            .type = ReceiverType::service,
            .key_hi = service_type,
            .key_lo = 1},
        scope,
        message);
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
    if (receiver_location.has_value() && receiver_location->kind == ReceiverLocationKind::single_process &&
        !receiver_location->processes.empty() && envelope.header.target_receiver.type != ReceiverType::process)
    {
        envelope.header.resolved_target_process = receiver_location->processes.front();
    }

    RoutingContext context;
    context.self = mSelf;
    context.envelope = envelope;
    context.receiver_location = std::move(receiver_location);
    context.membership = mMembership;
    context.links = mLinks;

    const RoutePlan plan = mRouter.Resolve(context);
    if (plan.kind == RoutePlanKind::local_delivery)
    {
        return DispatchLocal(envelope);
    }

    if (plan.kind == RoutePlanKind::drop)
    {
        return SendResult::Failure("message dropped during route resolution");
    }

    if (plan.kind == RoutePlanKind::single_next_hop)
    {
        if (mRemoteSender == nullptr)
        {
            return SendResult::Failure("remote message sender is not configured");
        }
        return mRemoteSender->Send(plan.hops.front().next_hop, envelope);
    }

    if (plan.kind == RoutePlanKind::multi_next_hop)
    {
        for (const auto& hop : plan.hops)
        {
            Envelope next_envelope = envelope;
            next_envelope.header.semantic = DeliverySemantic::direct;
            next_envelope.broadcast_scope = BroadcastScope{};
            if (next_envelope.header.target_receiver.type != ReceiverType::process)
            {
                next_envelope.header.resolved_target_process = hop.next_hop;
            }

            ReceiverLocation target_location;
            target_location.kind = ReceiverLocationKind::single_process;
            target_location.processes.push_back(hop.next_hop);
            target_location.version = 1;

            const SendResult send_result = SendEnvelope(std::move(next_envelope), std::move(target_location));
            if (!send_result.ok)
            {
                return send_result;
            }
        }
        return SendResult::Success();
    }

    return SendResult::Failure("route plan kind is not implemented yet");
}

DispatchResult Messenger::DispatchLocal(const Envelope& envelope) const
{
    if (!mPayloadRegistry.IsRegistered(envelope.payload_type_url))
    {
        return DispatchResult::Failure("payload type is not registered");
    }

    return mReceiverRegistry.Dispatch(envelope.header.target_receiver, envelope);
}

Result Messenger::HandleIncomingFrame(const RawFrame& frame) const
{
    if (frame.header.kind != FrameKind::data)
    {
        return Result::Failure("unsupported frame kind");
    }

    Envelope envelope;
    if (const Result decode_result = DecodeDataEnvelope(frame.payload, envelope); !decode_result.ok)
    {
        return decode_result;
    }

    std::optional<ReceiverLocation> receiver_location;
    if (envelope.header.target_receiver.type != ReceiverType::process)
    {
        if (envelope.header.resolved_target_process.has_value())
        {
            receiver_location = ReceiverLocation{
                .kind = ReceiverLocationKind::single_process,
                .processes = {*envelope.header.resolved_target_process},
                .version = 1};
        }
        else
        {
            receiver_location = mReceiverDirectory.Resolve(envelope.header.target_receiver);
        }
    }
    return SendEnvelope(std::move(envelope), std::move(receiver_location));
}
} // namespace ipc
