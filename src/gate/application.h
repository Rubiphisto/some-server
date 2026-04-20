#pragma once

#include "framework/application/application.h"

struct GateConfiguration : public BaseApplicationConfiguration, public JsonApplicationConfiguration<GateConfiguration>
{
};

class Application : public ApplicationBase<GateConfiguration>
{
public:
    std::string GetName() const override { return "gate"; }

protected:
    LifecycleTask OnUnload() override;
    LifecycleTask OnStart() override;
    LifecycleTask OnStop() override;
    LifecycleTask OnLoad() override;
};

SOME_SERVER_APPLICATION_CONFIG(GateConfiguration);
