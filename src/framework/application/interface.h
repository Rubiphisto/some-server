#pragma once

#include "configuration.h"

#include <memory>

class IApplication
{
public:
    virtual ~IApplication() = default;
    virtual const char8_t* GetName() const = 0;
    virtual std::unique_ptr<IApplicationConfiguration> CreateConfiguration() const = 0;
    virtual bool Configure(const IApplicationConfiguration& configuration) = 0;
    virtual void Load() = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Unload() = 0;
};
