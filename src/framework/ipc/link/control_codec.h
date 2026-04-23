#pragma once

#include "../base/process.h"
#include "../transport/frame.h"

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

struct ControlMessageHeader
{
    ControlMessageType type = ControlMessageType::hello;
    std::uint16_t reserved = 0;
};

struct HelloPayload
{
    // This milestone-1 skeleton still uses a tiny ad hoc payload for link bootstrap.
    // It will be replaced by the real protobuf control-plane messages later.
    ProcessRef self;
    std::uint32_t protocol_version = 1;
};

ByteBuffer EncodeControlMessage(ControlMessageType type, const ByteBuffer& payload);
bool DecodeControlMessage(const ByteBuffer& bytes, ControlMessageType& type, ByteBuffer& payload);
ByteBuffer EncodeHelloPayload(const HelloPayload& payload);
bool DecodeHelloPayload(const ByteBuffer& bytes, HelloPayload& payload);
} // namespace ipc
