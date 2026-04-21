#pragma once

#include "gate/application.h"
#include "framework/application/application.h"

class ListenerService final : public ServiceBase
{
public:
    explicit ListenerService(const GateConfiguration& configuration);

    LifecycleTask Load() override;
    LifecycleTask Start() override;
    LifecycleTask Stop() override;
    LifecycleTask Unload() override;

private:
    const GateConfiguration& mConfiguration;
};
