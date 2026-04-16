#pragma once

#include "framework/application/application.h"

class Application : public ApplicationBase
{
public:
    const char8_t* GetName() const override { return u8"gate"; }
    void Unload() override;
    void Start() override;
    void Stop() override;
    void Load() override;
};
