#include "auth_service.h"

GateAuthResult GateAuthService::Validate(
    const std::string_view platform,
    const std::string_view account_id,
    const std::string_view credential) const
{
    if (platform.empty())
    {
        return GateAuthResult{.ok = false, .account_id = std::string{account_id}, .message = "platform is empty"};
    }
    if (account_id.empty())
    {
        return GateAuthResult{.ok = false, .account_id = {}, .message = "account_id is empty"};
    }
    if (credential.empty())
    {
        return GateAuthResult{
            .ok = false,
            .account_id = std::string{account_id},
            .message = "credential is empty"};
    }

    return GateAuthResult{
        .ok = true,
        .account_id = std::string{account_id},
        .message = "OK"};
}
