#pragma once

#include "framework/application/application.h"

class Loader
{
public:
    int Run(IApplication& app, int argc, char* argv[]);

private:
    bool Initialize(IApplication& app, const IApplicationConfiguration& configuration) const;
};
