#pragma once

#include <functional>

#include "framework/application/application.h"

class Loader
{
public:
    explicit Loader(ApplicationFactory factory, ApplicationDestroyer destroyer = {});

    int Run(int argc, char* argv[]);

private:
    bool Initialize(IApplication& app, const ApplicationContext& context) const;
    ApplicationFactory mFactory;
    ApplicationDestroyer mDestroyer;
};
