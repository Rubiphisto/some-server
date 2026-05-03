#pragma once

#include "../base/process.h"
#include "../base/result.h"
#include "../transport/frame.h"

#include "ipc/control/v1/control.pb.h"

namespace ipc
{
// Stable internal control message discriminator used after protobuf decode.
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

// Minimal handshake state extracted from a Hello control message.
struct HelloInfo
{
    ProcessRef process_ref;
    std::uint32_t protocol_version = 0;
    std::uint32_t min_supported_protocol_version = 0;
};

// Encodes a Hello handshake for the local process and protocol version.
ByteBuffer EncodeHello(const ProcessRef& self, std::uint32_t protocol_version);
// Encodes a HelloAck handshake response, optionally carrying a reject result.
ByteBuffer EncodeHelloAck(const ProcessRef& self,
                         std::uint32_t protocol_version,
                         ProtoHelloAckResult result = some_server::ipc::control::v1::HelloAck_Result_RESULT_OK);
// Encodes a minimal Pong liveness response.
ByteBuffer EncodePong();
// Parses one protobuf control frame payload into the union control message.
bool DecodeControlMessage(const ByteBuffer& bytes, ProtoControlMessage& message);
// Maps the active protobuf variant to the internal control message type enum.
ControlMessageType GetControlMessageType(const ProtoControlMessage& message);
// Extracts handshake identity and protocol data from a Hello message.
Result ExtractHelloInfo(const ProtoControlMessage& message, HelloInfo& hello_info);
// Extracts the ack result code from a HelloAck message.
Result ExtractHelloAckResult(const ProtoControlMessage& message, ProtoHelloAckResult& result);
} // namespace ipc
