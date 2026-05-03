#pragma once

#include "../base/result.h"
#include "membership_view.h"

#include <unordered_map>

namespace ipc
{
// In-memory membership implementation used by local tests and simple skeletons.
class Discovery final : public IMembershipView
{
public:
    // Registers the local process into the in-memory member set.
    Result RegisterSelf(const ProcessDescriptor& self);
    // Adds or replaces one member snapshot entry.
    Result Upsert(const ProcessDescriptor& process);
    // Removes one process from the current member snapshot.
    Result Remove(const ProcessId& id);

    // Looks up one process by logical id in the current snapshot.
    std::optional<ProcessDescriptor> Find(const ProcessId& id) const override;
    // Returns all visible members of one service type.
    std::vector<ProcessDescriptor> FindByService(ServiceType type) const override;
    // Returns the full in-memory membership snapshot.
    std::vector<ProcessDescriptor> All() const override;
    // Drains accumulated add/update/remove events since the last call.
    std::vector<MembershipEvent> DrainEvents();

private:
    static std::uint64_t MakeKey(const ProcessId& id);

    std::unordered_map<std::uint64_t, ProcessDescriptor> mProcesses;
    std::vector<MembershipEvent> mEvents;
};
} // namespace ipc
