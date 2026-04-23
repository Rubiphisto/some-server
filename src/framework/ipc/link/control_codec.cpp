#include "control_codec.h"

#include <cstring>

namespace ipc
{
ByteBuffer EncodeControlMessage(ControlMessageType type, const ByteBuffer& payload)
{
    ByteBuffer bytes(sizeof(ControlMessageHeader) + payload.size());
    const ControlMessageHeader header{type, 0};
    std::memcpy(bytes.data(), &header, sizeof(header));
    if (!payload.empty())
    {
        std::memcpy(bytes.data() + sizeof(header), payload.data(), payload.size());
    }
    return bytes;
}

bool DecodeControlMessage(const ByteBuffer& bytes, ControlMessageType& type, ByteBuffer& payload)
{
    if (bytes.size() < sizeof(ControlMessageHeader))
    {
        return false;
    }

    ControlMessageHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    type = header.type;
    payload.resize(bytes.size() - sizeof(header));
    if (!payload.empty())
    {
        std::memcpy(payload.data(), bytes.data() + sizeof(header), payload.size());
    }
    return true;
}

ByteBuffer EncodeHelloPayload(const HelloPayload& payload)
{
    ByteBuffer bytes(sizeof(payload));
    std::memcpy(bytes.data(), &payload, sizeof(payload));
    return bytes;
}

bool DecodeHelloPayload(const ByteBuffer& bytes, HelloPayload& payload)
{
    if (bytes.size() != sizeof(payload))
    {
        return false;
    }

    std::memcpy(&payload, bytes.data(), sizeof(payload));
    return true;
}
} // namespace ipc
