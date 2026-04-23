#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ipc
{
using ServiceType = std::uint32_t;
using InstanceId = std::uint32_t;
using IncarnationId = std::uint64_t;
using ReceiverKeyPart = std::uint64_t;
using ConnectionId = std::uint64_t;
using RequestId = std::uint64_t;

using ByteBuffer = std::vector<std::byte>;
using StringMap = std::vector<std::pair<std::string, std::string>>;
} // namespace ipc
