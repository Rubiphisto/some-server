#include "data_codec.h"

#include "ipc/data/v1/envelope.pb.h"

namespace ipc
{
namespace
{
using ProtoDataEnvelope = some_server::ipc::data::v1::DataEnvelope;
using ProtoReceiverAddress = some_server::ipc::common::v1::ReceiverAddress;
using ProtoBroadcastScope = some_server::ipc::common::v1::BroadcastScope;
using ProtoProcessRef = some_server::ipc::common::v1::ProcessRef;

ProtoProcessRef ToProto(const ProcessRef& process)
{
    ProtoProcessRef proto;
    proto.set_service_type(process.process_id.service_type);
    proto.set_instance_id(process.process_id.instance_id);
    proto.set_incarnation_id(process.incarnation_id);
    return proto;
}

ProcessRef FromProto(const ProtoProcessRef& proto)
{
    return ProcessRef{
        .process_id = ProcessId{
            .service_type = proto.service_type(),
            .instance_id = proto.instance_id()},
        .incarnation_id = proto.incarnation_id()};
}

ProtoReceiverAddress ToProto(const ReceiverAddress& receiver)
{
    ProtoReceiverAddress proto;
    proto.set_type(static_cast<std::uint32_t>(receiver.type));
    proto.set_key_hi(receiver.key_hi);
    proto.set_key_lo(receiver.key_lo);
    return proto;
}

ReceiverAddress FromProto(const ProtoReceiverAddress& proto)
{
    return ReceiverAddress{
        .type = static_cast<ReceiverType>(proto.type()),
        .key_hi = proto.key_hi(),
        .key_lo = proto.key_lo()};
}

void ToProto(const BroadcastScope& scope, ProtoBroadcastScope& proto)
{
    if (scope.service_type.has_value())
    {
        proto.set_service_type(*scope.service_type);
    }
    for (const auto& [key, value] : scope.required_labels)
    {
        (*proto.mutable_required_labels())[key] = value;
    }
    proto.set_include_local(scope.include_local);
}

BroadcastScope FromProto(const ProtoBroadcastScope& proto)
{
    BroadcastScope scope;
    if (proto.has_service_type())
    {
        scope.service_type = proto.service_type();
    }
    for (const auto& [key, value] : proto.required_labels())
    {
        scope.required_labels.emplace_back(key, value);
    }
    scope.include_local = proto.include_local();
    return scope;
}
} // namespace

ByteBuffer EncodeDataEnvelope(const Envelope& envelope)
{
    ProtoDataEnvelope proto;
    *proto.mutable_source_process() = ToProto(envelope.header.source_process);
    proto.set_semantic(
        envelope.header.semantic == DeliverySemantic::broadcast
            ? ProtoDataEnvelope::DELIVERY_SEMANTIC_BROADCAST
            : ProtoDataEnvelope::DELIVERY_SEMANTIC_DIRECT);
    *proto.mutable_target_receiver() = ToProto(envelope.header.target_receiver);
    if (envelope.header.semantic == DeliverySemantic::broadcast)
    {
        ToProto(envelope.broadcast_scope, *proto.mutable_broadcast_scope());
    }
    proto.set_payload_type_url(envelope.payload_type_url);
    proto.set_payload_bytes(
        reinterpret_cast<const char*>(envelope.payload_bytes.data()),
        static_cast<int>(envelope.payload_bytes.size()));
    proto.set_request_id(envelope.header.request_id);
    proto.set_flags(envelope.header.flags);

    const std::string serialized = proto.SerializeAsString();
    return ByteBuffer(
        reinterpret_cast<const std::byte*>(serialized.data()),
        reinterpret_cast<const std::byte*>(serialized.data() + serialized.size()));
}

Result DecodeDataEnvelope(const ByteBuffer& bytes, Envelope& envelope)
{
    ProtoDataEnvelope proto;
    if (!proto.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
    {
        return Result::Failure("invalid data envelope");
    }

    envelope.header.source_process = FromProto(proto.source_process());
    envelope.header.semantic = proto.semantic() == ProtoDataEnvelope::DELIVERY_SEMANTIC_BROADCAST
        ? DeliverySemantic::broadcast
        : DeliverySemantic::direct;
    envelope.header.target_receiver = FromProto(proto.target_receiver());
    if (proto.has_broadcast_scope())
    {
        envelope.broadcast_scope = FromProto(proto.broadcast_scope());
    }
    else
    {
        envelope.broadcast_scope = BroadcastScope{};
    }
    envelope.payload_type_url = proto.payload_type_url();
    envelope.payload_bytes.assign(
        reinterpret_cast<const std::byte*>(proto.payload_bytes().data()),
        reinterpret_cast<const std::byte*>(proto.payload_bytes().data() + proto.payload_bytes().size()));
    envelope.header.request_id = proto.request_id();
    envelope.header.flags = proto.flags();
    return Result::Success();
}
} // namespace ipc
