#pragma once

#include "interface.h"

#include <cassert>
#include <memory>

template <typename TConfiguration>
class ApplicationBase : public IApplication
{
public:
    std::unique_ptr<IApplicationConfiguration> CreateConfiguration() const override
    {
        return std::make_unique<TConfiguration>();
    }

    void SetRuntime(IApplicationRuntime& runtime) override
    {
        mRuntime = &runtime;
    }

    bool Configure(const CommonConfiguration& common_configuration,
                   const IApplicationConfiguration& application_configuration) override
    {
        const auto* typed = dynamic_cast<const TConfiguration*>(&application_configuration);
        if (typed == nullptr)
        {
            return false;
        }

        mCommonConfiguration = common_configuration;
        mApplicationConfiguration = *typed;
        return OnConfigure();
    }

protected:
    virtual bool OnConfigure() { return true; }
    const CommonConfiguration& CommonConfig() const { return mCommonConfiguration; }
    const TConfiguration& AppConfig() const { return mApplicationConfiguration; }
    IApplicationRuntime& Runtime() const
    {
        assert(mRuntime != nullptr && "Application runtime has not been set");
        return *mRuntime;
    }

private:
    IApplicationRuntime* mRuntime = nullptr;
    CommonConfiguration mCommonConfiguration;
    TConfiguration mApplicationConfiguration;
};
