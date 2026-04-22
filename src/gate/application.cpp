#include "application.h"
#include "services/listener_service.h"

#include <spdlog/spdlog.h>

#include <stdexcept>

void Application::RegisterServices()
{
    AddService(std::make_unique<ListenerService>(AppConfig()));
}

void Application::RegisterRuntimeCommands()
{
    const bool registered = Runtime().RegisterCommand(
        "status",
        "Show gate runtime status",
        [](const CommandArguments&) {
            spdlog::info("gate status: {}", "running");
            return CommandExecutionStatus::handled;
        });

    if (!registered)
    {
        throw std::runtime_error("failed to register gate status command");
    }
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
