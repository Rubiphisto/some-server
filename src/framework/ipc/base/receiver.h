#pragma once

#include "process.h"

#include <optional>

namespace ipc
{
// Classifies the logical receiver namespace used by upper-layer IPC APIs.
enum class ReceiverType : std::uint16_t
{
    process = 1,
    player = 2,
    system = 3,
    service = 4,
    group = 5
};

// Stable receiver address understood by routing and receiver directory layers.
struct ReceiverAddress
{
    ReceiverType type = ReceiverType::process;
    ReceiverKeyPart key_hi = 0;
    ReceiverKeyPart key_lo = 0;

    friend bool operator==(const ReceiverAddress&, const ReceiverAddress&) = default;
};

// Describes whether a receiver currently resolves locally, remotely, or not at all.
enum class ReceiverLocationKind : std::uint8_t
{
    local,
    single_process,
    multi_process,
    unresolved
};

// Resolution result returned by the receiver directory for one receiver address.
struct ReceiverLocation
{
    ReceiverLocationKind kind = ReceiverLocationKind::unresolved;
    std::vector<ProcessRef> processes;
    std::uint64_t version = 0;
};

// Broadcast filter used to expand one logical broadcast into target processes.
struct BroadcastScope
{
    std::optional<ServiceType> service_type;
    StringMap required_labels;
    bool include_local = true;
};
} // namespace ipc
