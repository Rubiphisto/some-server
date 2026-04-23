#pragma once

#include "../base/result.h"
#include "membership_view.h"

#include <unordered_map>

namespace ipc
{
class Discovery final : public IMembershipView
{
public:
    Result RegisterSelf(const ProcessDescriptor& self);
    Result Upsert(const ProcessDescriptor& process);
    Result Remove(const ProcessId& id);

    std::optional<ProcessDescriptor> Find(const ProcessId& id) const override;
    std::vector<ProcessDescriptor> FindByService(ServiceType type) const override;
    std::vector<ProcessDescriptor> All() const override;
    std::vector<MembershipEvent> DrainEvents();

private:
    static std::uint64_t MakeKey(const ProcessId& id);

    std::unordered_map<std::uint64_t, ProcessDescriptor> mProcesses;
    std::vector<MembershipEvent> mEvents;
};
} // namespace ipc
