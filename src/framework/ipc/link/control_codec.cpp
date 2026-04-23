#include "control_codec.h"

namespace ipc
{
namespace
{
void FillProcessIdentity(const ProcessRef& source, some_server::ipc::common::v1::ProcessIdentity& target)
{
    target.set_service_type(source.process_id.service_type);
    target.set_instance_id(source.process_id.instance_id);
    target.set_incarnation_id(source.incarnation_id);
}

ProcessRef ToProcessRef(const some_server::ipc::common::v1::ProcessIdentity& identity)
{
    return ProcessRef{
        ProcessId{identity.service_type(), identity.instance_id()},
        identity.incarnation_id()};
}

ByteBuffer SerializeMessage(const ProtoControlMessage& message)
{
    ByteBuffer bytes(static_cast<std::size_t>(message.ByteSizeLong()));
    if (!bytes.empty())
    {
        message.SerializeToArray(bytes.data(), static_cast<int>(bytes.size()));
    }
    return bytes;
}
} // namespace

ByteBuffer EncodeHello(const ProcessRef& self, std::uint32_t protocol_version)
{
    ProtoControlMessage message;
    auto* hello = message.mutable_hello();
    FillProcessIdentity(self, *hello->mutable_self());
    hello->set_protocol_version(protocol_version);
    hello->set_min_supported_protocol_version(protocol_version);
    return SerializeMessage(message);
}

ByteBuffer EncodeHelloAck(const ProcessRef& self, std::uint32_t protocol_version, ProtoHelloAckResult result)
{
    ProtoControlMessage message;
    auto* hello_ack = message.mutable_hello_ack();
    hello_ack->set_result(result);
    FillProcessIdentity(self, *hello_ack->mutable_self());
    hello_ack->set_protocol_version(protocol_version);
    return SerializeMessage(message);
}

ByteBuffer EncodePong()
{
    ProtoControlMessage message;
    message.mutable_pong();
    return SerializeMessage(message);
}

bool DecodeControlMessage(const ByteBuffer& bytes, ProtoControlMessage& message)
{
    return message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()));
}

ControlMessageType GetControlMessageType(const ProtoControlMessage& message)
{
    switch (message.body_case())
    {
    case ProtoControlMessage::kHello:
        return ControlMessageType::hello;
    case ProtoControlMessage::kHelloAck:
        return ControlMessageType::hello_ack;
    case ProtoControlMessage::kPing:
        return ControlMessageType::ping;
    case ProtoControlMessage::kPong:
        return ControlMessageType::pong;
    case ProtoControlMessage::kClose:
        return ControlMessageType::close;
    case ProtoControlMessage::BODY_NOT_SET:
        break;
    }

    return ControlMessageType::close;
}

Result ExtractHelloInfo(const ProtoControlMessage& message, HelloInfo& hello_info)
{
    if (message.body_case() != ProtoControlMessage::kHello || !message.hello().has_self())
    {
        return Result::Failure("hello message missing process identity");
    }

    hello_info.process_ref = ToProcessRef(message.hello().self());
    hello_info.protocol_version = message.hello().protocol_version();
    hello_info.min_supported_protocol_version = message.hello().min_supported_protocol_version();
    return Result::Success();
}

Result ExtractHelloAckResult(const ProtoControlMessage& message, ProtoHelloAckResult& result)
{
    if (message.body_case() != ProtoControlMessage::kHelloAck)
    {
        return Result::Failure("hello_ack message missing result");
    }

    result = message.hello_ack().result();
    return Result::Success();
}
} // namespace ipc
