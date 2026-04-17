#pragma once

#include "framework/application/application.h"

struct GateConfiguration : public IApplicationConfiguration
{
    std::string listen_host = "127.0.0.1";
    std::size_t listen_port = 9000;

    bool OverlayFromConfig(const ConfigValue& root, std::string& error) override;
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
