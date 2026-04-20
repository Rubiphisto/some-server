#pragma once

#include "lifecycle_task.h"

#include <cstdint>
#include <string_view>

class IService
{
public:
    virtual ~IService() = default;

    virtual std::string_view GetName() const = 0;
    virtual std::int32_t GetBatch() const = 0;

    virtual LifecycleTask Load() = 0;
    virtual LifecycleTask Start() = 0;
    virtual LifecycleTask Stop() = 0;
    virtual LifecycleTask Unload() = 0;
};
