#pragma once

#include "service.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

class ServiceBase : public IService
{
public:
    ServiceBase(std::string name, std::int32_t batch) : mName(std::move(name)), mBatch(batch) {}

    std::string_view GetName() const final
    {
        return mName;
    }

    std::int32_t GetBatch() const final
    {
        return mBatch;
    }

    LifecycleTask Load() override { return LifecycleTask::Completed(); }
    LifecycleTask Start() override { return LifecycleTask::Completed(); }
    LifecycleTask Stop() override { return LifecycleTask::Completed(); }
    LifecycleTask Unload() override { return LifecycleTask::Completed(); }

private:
    std::string mName;
    std::int32_t mBatch = 0;
};
