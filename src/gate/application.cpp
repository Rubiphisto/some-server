#include "application.h"

#include <spdlog/spdlog.h>

namespace
{
class GateLifecycleService final : public ServiceBase
{
public:
    GateLifecycleService() : ServiceBase("gate.lifecycle", 0) {}

    LifecycleTask Load() override
    {
        spdlog::info("GateLifecycleService::Load()");
        return LifecycleTask::Completed();
    }

    LifecycleTask Start() override
    {
        spdlog::info("GateLifecycleService::Start()");
        return LifecycleTask::Completed();
    }

    LifecycleTask Stop() override
    {
        spdlog::info("GateLifecycleService::Stop()");
        return LifecycleTask::Completed();
    }

    LifecycleTask Unload() override
    {
        spdlog::info("GateLifecycleService::Unload()");
        return LifecycleTask::Completed();
    }
};
}

void Application::RegisterServices()
{
    AddService(std::make_unique<GateLifecycleService>());
}

LifecycleTask Application::OnLoad()
{
    spdlog::info("Application::Configure(listen={}:{})",
                 AppConfig().listen.host,
                 AppConfig().listen.port);
    spdlog::info("Application::Load()");
    return LifecycleTask::Completed();
}

LifecycleTask Application::OnStart()
{
    spdlog::info("Application::Start()");
    return LifecycleTask::Completed();
}

LifecycleTask Application::OnStop()
{
    spdlog::info("Application::Stop()");
    return LifecycleTask::Completed();
}

LifecycleTask Application::OnUnload()
{
    spdlog::info("Application::Unload()");
    return LifecycleTask::Completed();
}
