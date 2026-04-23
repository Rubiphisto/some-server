#pragma once

#include "../base/process.h"

#include <optional>
#include <vector>

namespace ipc
{
enum class MembershipEventType : std::uint8_t
{
    added,
    updated,
    removed
};

struct MembershipEvent
{
    MembershipEventType type = MembershipEventType::added;
    ProcessDescriptor process;
};

class IMembershipView
{
public:
    virtual ~IMembershipView() = default;

    virtual std::optional<ProcessDescriptor> Find(const ProcessId& id) const = 0;
    virtual std::vector<ProcessDescriptor> FindByService(ServiceType type) const = 0;
    virtual std::vector<ProcessDescriptor> All() const = 0;
};
} // namespace ipc
