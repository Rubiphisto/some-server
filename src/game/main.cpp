#include "pch.h"

#include "application.h"
#include "../common/basis/singleton.h"
#include "../loader/loader.h"

namespace
{
    using ApplicationSingleton = Common::Singleton<Application>;
}

int32_t main(int32_t argc, char* argv[])
{
    Application& app = ApplicationSingleton::Create();

    Loader loader;
    const int32_t exit_code = loader.Run(app, argc, argv);

    ApplicationSingleton::Destroy();
    return exit_code;
}
