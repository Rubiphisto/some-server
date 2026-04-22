#pragma once

#include "framework/application/application.h"

struct GameConfiguration : public BaseApplicationConfiguration, public JsonApplicationConfiguration<GameConfiguration>
{
};

class Application : public ApplicationBase<GameConfiguration>
{
public:
    std::string GetName() const override { return "game"; }

protected:
    void RegisterRuntimeCommands() override;
    LifecycleTask OnUnload() override;
    LifecycleTask OnStart() override;
    LifecycleTask OnStop() override;
    LifecycleTask OnLoad() override;
};

SOME_SERVER_APPLICATION_CONFIG(GameConfiguration);
