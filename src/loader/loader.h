#pragma once

#include "framework/application/application.h"

class Loader
{
public:
    explicit Loader(IApplicationFactory& factory);

    int Run(int argc, char* argv[]);

private:
    bool Initialize(IApplication& app, const ApplicationContext& context) const;
    IApplicationFactory& mFactory;
};
