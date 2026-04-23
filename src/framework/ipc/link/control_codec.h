#pragma once

#include "../base/process.h"
#include "../base/result.h"
#include "../transport/frame.h"

#include "ipc/control/v1/control.pb.h"

namespace ipc
{
enum class ControlMessageType : std::uint16_t
{
    hello = 1,
    hello_ack = 2,
    ping = 3,
    pong = 4,
    close = 5
};

using ProtoControlMessage = some_server::ipc::control::v1::ControlMessage;
using ProtoHelloAckResult = some_server::ipc::control::v1::HelloAck_Result;

struct HelloInfo
{
    ProcessRef process_ref;
    std::uint32_t protocol_version = 0;
    std::uint32_t min_supported_protocol_version = 0;
};

ByteBuffer EncodeHello(const ProcessRef& self, std::uint32_t protocol_version);
ByteBuffer EncodeHelloAck(const ProcessRef& self,
                         std::uint32_t protocol_version,
                         ProtoHelloAckResult result = some_server::ipc::control::v1::HelloAck_Result_RESULT_OK);
ByteBuffer EncodePong();
bool DecodeControlMessage(const ByteBuffer& bytes, ProtoControlMessage& message);
ControlMessageType GetControlMessageType(const ProtoControlMessage& message);
Result ExtractHelloInfo(const ProtoControlMessage& message, HelloInfo& hello_info);
Result ExtractHelloAckResult(const ProtoControlMessage& message, ProtoHelloAckResult& result);
} // namespace ipc
