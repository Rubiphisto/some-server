#pragma once

#include "../../framework/application/configuration.h"
#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/result.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

struct redisContext;

struct GateAccountOwner
{
    std::uint32_t gate_service_type = 0;
    std::uint32_t gate_instance_id = 0;
    std::uint64_t gate_session_id = 0;
};

class GateAccountDirectoryService final : public ServiceBase
{
public:
    GateAccountDirectoryService(const CommonConfiguration& common_configuration, std::string redis_name)
        : ServiceBase("gate_account_directory", 15)
        , mCommonConfiguration(common_configuration)
        , mRedisName(std::move(redis_name))
    {
    }

    LifecycleTask Load() override;
    LifecycleTask Unload() override;

    std::optional<GateAccountOwner> LookupAccount(std::string_view account_id);
    ipc::Result BindAccount(std::string_view account_id, const GateAccountOwner& owner);
    ipc::Result RemoveAccountIfMatches(std::string_view account_id, const GateAccountOwner& owner);

private:
    static std::string BuildValue(const GateAccountOwner& owner);
    static std::optional<GateAccountOwner> ParseValue(std::string_view value);

    const CommonConfiguration& mCommonConfiguration;
    std::string mRedisName;
    std::string mRedisKeyPrefix;
    mutable std::mutex mMutex;
    redisContext* mRedis = nullptr;
};
