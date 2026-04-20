#pragma once

#include "configuration.h"
#include "runtime.h"

#include <memory>
#include <string>

class IApplication
{
public:
    virtual ~IApplication() = default;
    virtual std::string GetName() const = 0;
    virtual std::unique_ptr<IApplicationConfiguration> CreateConfiguration() const = 0;
    virtual void SetRuntime(IApplicationRuntime& runtime) = 0;
    virtual bool Configure(const CommonConfiguration& common_configuration,
                           const IApplicationConfiguration& application_configuration) = 0;
    virtual void Load() = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Unload() = 0;
};
