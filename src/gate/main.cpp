#include "pch.h"

#include "application.h"
#include "../common/basis/singleton.h"
#include "../loader/loader.h"

namespace
{
    using ApplicationSingleton = Common::Singleton<Application>;

    IApplication* CreateApplication()
    {
        return &ApplicationSingleton::Create();
    }

    void DestroyApplication(IApplication*)
    {
        ApplicationSingleton::Destroy();
    }
}

int32_t main(int32_t argc, char* argv[])
{
    Loader loader([]() { return CreateApplication(); }, [](IApplication* app) { DestroyApplication(app); });
    return loader.Run(argc, argv);
}
