#pragma once

#include "../base/process.h"

#include <optional>
#include <vector>

namespace ipc
{
// Describes how one process changed inside the current membership view.
enum class MembershipEventType : std::uint8_t
{
    added,
    updated,
    removed
};

// One membership transition emitted by discovery after snapshot diffing.
struct MembershipEvent
{
    MembershipEventType type = MembershipEventType::added;
    ProcessDescriptor process;
};

// Read-only membership snapshot consumed by routing and app-level topology logic.
class IMembershipView
{
public:
    virtual ~IMembershipView() = default;

    // Finds one process descriptor by logical process id.
    virtual std::optional<ProcessDescriptor> Find(const ProcessId& id) const = 0;
    // Returns all currently visible processes of the given service type.
    virtual std::vector<ProcessDescriptor> FindByService(ServiceType type) const = 0;
    // Returns the complete current process snapshot.
    virtual std::vector<ProcessDescriptor> All() const = 0;
};
} // namespace ipc
