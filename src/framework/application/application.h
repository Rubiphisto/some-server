#pragma once

#include "framework/config/value.h"

#include <memory>
#include <string>

class IApplicationConfiguration
{
public:
    virtual ~IApplicationConfiguration() = default;
    virtual bool OverlayFromConfig(const ConfigValue& root, std::string& error) = 0;
};

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

template <typename TConfiguration>
class ApplicationBase : public IApplication
{
public:
    std::unique_ptr<IApplicationConfiguration> CreateConfiguration() const override
    {
        return std::make_unique<TConfiguration>();
    }

    bool Configure(const IApplicationConfiguration& configuration) override
    {
        const auto* typed = dynamic_cast<const TConfiguration*>(&configuration);
        if (typed == nullptr)
        {
            return false;
        }

        mConfiguration = *typed;
        return OnConfigure();
    }

protected:
    virtual bool OnConfigure() { return true; }
    const TConfiguration& AppConfig() const { return mConfiguration; }

private:
    TConfiguration mConfiguration;
};
