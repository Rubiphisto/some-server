#pragma once

#include "framework/application/application.h"

struct GateConfiguration : public BaseApplicationConfiguration, public JsonApplicationConfiguration<GateConfiguration>
{
};

class Application : public ApplicationBase<GateConfiguration>
{
public:
    const char8_t* GetName() const override { return u8"gate"; }
    void Unload() override;
    void Start() override;
    void Stop() override;
    void Load() override;
};

SOME_SERVER_APPLICATION_CONFIG(GateConfiguration);
