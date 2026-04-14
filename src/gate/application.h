#pragma once

#include <memory>

#include "framework/application/application.h"

class Application : public IApplication
{
public:
    const char8_t* GetName() const override { return u8"gate"; }
    bool Configure(const ApplicationContext& context) override;
    void Unload() override;
    void Start() override;
    void Stop() override;
    void Load() override;

private:
    ApplicationContext mContext;
};

class ApplicationFactory : public IApplicationFactory
{
public:
    std::unique_ptr<IApplication> Create() override;
};
