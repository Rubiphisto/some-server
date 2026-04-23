#pragma once

#include "framework/application/application.h"

struct RelayConfiguration : public BaseApplicationConfiguration, public JsonApplicationConfiguration<RelayConfiguration>
{
};

class Application : public ApplicationBase<RelayConfiguration>
{
public:
    std::string GetName() const override { return "relay"; }

protected:
    void RegisterRuntimeCommands() override;
};

SOME_SERVER_APPLICATION_CONFIG(RelayConfiguration);
