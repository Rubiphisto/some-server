#pragma once

#include "process.h"

#include <optional>

namespace ipc
{
enum class ReceiverType : std::uint16_t
{
    process = 1,
    player = 2,
    system = 3,
    service = 4,
    group = 5
};

struct ReceiverAddress
{
    ReceiverType type = ReceiverType::process;
    ReceiverKeyPart key_hi = 0;
    ReceiverKeyPart key_lo = 0;

    friend bool operator==(const ReceiverAddress&, const ReceiverAddress&) = default;
};

enum class ReceiverLocationKind : std::uint8_t
{
    local,
    single_process,
    multi_process,
    unresolved
};

struct ReceiverLocation
{
    ReceiverLocationKind kind = ReceiverLocationKind::unresolved;
    std::vector<ProcessRef> processes;
    std::uint64_t version = 0;
};

struct BroadcastScope
{
    std::optional<ServiceType> service_type;
    StringMap required_labels;
    bool include_local = true;
};
} // namespace ipc
