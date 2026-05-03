#pragma once

#include "../../framework/ipc/base/process.h"

#include <cstddef>
#include <vector>

namespace some_server::common
{
// Shared first-phase topology helpers used by app-level auto-connect glue.
class FirstPhaseIpcTopologyPolicy
{
public:
    // Returns whether the current member snapshot already contains the service type.
    static bool HasMemberOfServiceType(
        const std::vector<::ipc::ProcessDescriptor>& members,
        const ::ipc::ServiceType service_type)
    {
        for (const auto& member : members)
        {
            if (member.process.process_id.service_type == service_type)
            {
                return true;
            }
        }
        return false;
    }

    // Returns whether the current healthy links already satisfy the service type.
    static bool HasHealthyLinkOfServiceType(
        const std::vector<::ipc::ProcessRef>& links,
        const ::ipc::ServiceType service_type)
    {
        for (const auto& link : links)
        {
            if (link.process_id.service_type == service_type)
            {
                return true;
            }
        }
        return false;
    }

    // Game should reconcile whenever relay visibility or relay connectivity is missing.
    static bool ShouldGameReconcile(const bool relay_member_visible, const bool healthy_relay_link)
    {
        return !healthy_relay_link || !relay_member_visible;
    }

    // Decides whether a discovered member should be auto-connected as the game's relay target.
    static bool ShouldGameAutoConnectTarget(
        const ::ipc::ProcessRef& self,
        const ::ipc::ProcessDescriptor& member,
        const ::ipc::ServiceType relay_service_type,
        const bool healthy_relay_link)
    {
        if (member.process == self)
        {
            return false;
        }
        if (member.process.process_id.service_type != relay_service_type)
        {
            return false;
        }
        if (healthy_relay_link)
        {
            return false;
        }
        return true;
    }

    // Decides whether relay should proactively connect to a discovered game member.
    static bool ShouldRelayAutoConnectTarget(
        const ::ipc::ProcessRef& self,
        const ::ipc::ProcessDescriptor& member,
        const ::ipc::ServiceType game_service_type,
        const bool already_has_healthy_link)
    {
        if (member.process == self)
        {
            return false;
        }
        if (member.process.process_id.service_type != game_service_type)
        {
            return false;
        }
        if (already_has_healthy_link)
        {
            return false;
        }
        return true;
    }
};
} // namespace some_server::common
