#pragma once

#include "interface.h"

#include <memory>

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
