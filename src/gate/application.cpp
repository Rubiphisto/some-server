#include "application.h"
#include "services/listener_service.h"
#include "services/runtime_command_service.h"

#include <spdlog/spdlog.h>

void Application::RegisterServices()
{
    AddService(std::make_unique<RuntimeCommandService>(Runtime()));
    AddService(std::make_unique<ListenerService>(AppConfig()));
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
    spdlog::info("Application::OnStart()");
    return LifecycleTask::Completed();
}

LifecycleTask Application::OnStop()
{
    spdlog::info("Application::OnStop()");
    return LifecycleTask::Completed();
}

LifecycleTask Application::OnUnload()
{
    spdlog::info("Application::OnUnload()");
    return LifecycleTask::Completed();
}
