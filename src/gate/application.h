#pragma once

#include "framework/application/application.h"

struct GateConfiguration : public BaseApplicationConfiguration, public JsonApplicationConfiguration<GateConfiguration>
{
};

class Application : public ApplicationBase<GateConfiguration>
{
public:
    std::string GetName() const override { return "gate"; }
    void Unload() override;
    void Start() override;
    void Stop() override;
    void Load() override;
};

SOME_SERVER_APPLICATION_CONFIG(GateConfiguration);
