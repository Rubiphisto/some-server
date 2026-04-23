#include "discovery.h"

namespace ipc
{
Result Discovery::RegisterSelf(const ProcessDescriptor& self)
{
    return Upsert(self);
}

Result Discovery::Upsert(const ProcessDescriptor& process)
{
    const std::uint64_t key = MakeKey(process.process.process_id);
    const auto it = mProcesses.find(key);
    const MembershipEventType event_type =
        it == mProcesses.end() ? MembershipEventType::added : MembershipEventType::updated;

    mProcesses[key] = process;
    mEvents.push_back(MembershipEvent{event_type, process});
    return Result::Success();
}

Result Discovery::Remove(const ProcessId& id)
{
    const std::uint64_t key = MakeKey(id);
    const auto it = mProcesses.find(key);
    if (it == mProcesses.end())
    {
        return Result::Failure("process not found");
    }

    mEvents.push_back(MembershipEvent{MembershipEventType::removed, it->second});
    mProcesses.erase(it);
    return Result::Success();
}

std::optional<ProcessDescriptor> Discovery::Find(const ProcessId& id) const
{
    const std::uint64_t key = MakeKey(id);
    const auto it = mProcesses.find(key);
    if (it == mProcesses.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::vector<ProcessDescriptor> Discovery::FindByService(ServiceType type) const
{
    std::vector<ProcessDescriptor> processes;
    for (const auto& [_, process] : mProcesses)
    {
        if (process.process.process_id.service_type == type)
        {
            processes.push_back(process);
        }
    }
    return processes;
}

std::vector<ProcessDescriptor> Discovery::All() const
{
    std::vector<ProcessDescriptor> processes;
    processes.reserve(mProcesses.size());
    for (const auto& [_, process] : mProcesses)
    {
        processes.push_back(process);
    }
    return processes;
}

std::vector<MembershipEvent> Discovery::DrainEvents()
{
    std::vector<MembershipEvent> events = std::move(mEvents);
    mEvents.clear();
    return events;
}

std::uint64_t Discovery::MakeKey(const ProcessId& id)
{
    return (static_cast<std::uint64_t>(id.service_type) << 32) | id.instance_id;
}
} // namespace ipc
