#pragma once

#include "../base/types.h"

#include <array>
#include <cstring>

namespace ipc
{
enum class FrameKind : std::uint16_t
{
    control = 1,
    data = 2
};

inline constexpr std::uint32_t kFrameMagic = 0x53495043; // "SIPC"
inline constexpr std::uint16_t kFrameVersion = 1;
inline constexpr std::size_t kFrameHeaderSize = 12;

struct FrameHeader
{
    std::uint32_t magic = kFrameMagic;
    std::uint16_t version = kFrameVersion;
    FrameKind kind = FrameKind::control;
    std::uint32_t length = 0;
};

struct RawFrame
{
    ConnectionId connection_id = 0;
    FrameHeader header;
    ByteBuffer payload;
};

inline std::array<std::byte, kFrameHeaderSize> SerializeFrameHeader(const FrameHeader& header)
{
    // The frame header is the only part of the IPC wire format that is represented
    // as a tiny fixed native struct. Higher-level protocol payloads stay protobuf-based.
    std::array<std::byte, kFrameHeaderSize> bytes{};
    std::memcpy(bytes.data(), &header.magic, sizeof(header.magic));
    std::memcpy(bytes.data() + 4, &header.version, sizeof(header.version));

    const auto kind = static_cast<std::uint16_t>(header.kind);
    std::memcpy(bytes.data() + 6, &kind, sizeof(kind));
    std::memcpy(bytes.data() + 8, &header.length, sizeof(header.length));
    return bytes;
}

inline bool DeserializeFrameHeader(const std::byte* data, std::size_t size, FrameHeader& header)
{
    if (data == nullptr || size < kFrameHeaderSize)
    {
        return false;
    }

    std::memcpy(&header.magic, data, sizeof(header.magic));
    std::memcpy(&header.version, data + 4, sizeof(header.version));

    std::uint16_t kind = 0;
    std::memcpy(&kind, data + 6, sizeof(kind));
    header.kind = static_cast<FrameKind>(kind);
    std::memcpy(&header.length, data + 8, sizeof(header.length));

    return header.magic == kFrameMagic;
}
} // namespace ipc
