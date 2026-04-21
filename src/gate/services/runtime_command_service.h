#pragma once

#include "framework/application/application.h"

class RuntimeCommandService final : public ServiceBase
{
public:
    explicit RuntimeCommandService(IApplicationRuntime& runtime);

    LifecycleTask Load() override;
    LifecycleTask Start() override;
    LifecycleTask Stop() override;
    LifecycleTask Unload() override;

private:
    IApplicationRuntime& mRuntime;
    bool mRunning = false;
};
