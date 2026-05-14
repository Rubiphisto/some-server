#pragma once

#include "../../framework/application/service_base.h"

#include <string>
#include <string_view>

struct GateAuthResult
{
    bool ok = false;
    std::string account_id;
    std::string message;
};

class GateAuthService final : public ServiceBase
{
public:
    GateAuthService()
        : ServiceBase("gate_auth", 50)
    {
    }

    GateAuthResult Validate(std::string_view platform, std::string_view account_id, std::string_view credential) const;
};
