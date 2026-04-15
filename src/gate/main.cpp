#include "pch.h"

#include "application.h"
#include "../loader/loader.h"

namespace
{
    IApplication* CreateApplication()
    {
        return new Application();
    }
}

int32_t main(int32_t argc, char* argv[])
{
    Loader loader([]() { return CreateApplication(); });
    return loader.Run(argc, argv);
}
