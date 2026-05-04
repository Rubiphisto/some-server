#include "application.h"

#include <spdlog/spdlog.h>

void Application::RegisterServices()
{
}

void Application::RegisterRuntimeCommands()
{
    Runtime().RegisterCommand(
        "status",
        "Show gate runtime status",
        [](const CommandArguments&) {
            spdlog::info("gate status: {}", "running");
            return CommandExecutionStatus::handled;
        });
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
