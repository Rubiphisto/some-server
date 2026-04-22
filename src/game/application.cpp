#include "application.h"

#include <spdlog/spdlog.h>

#include <stdexcept>

void Application::RegisterRuntimeCommands()
{
    const bool registered = Runtime().RegisterCommand(
        "status",
        "Show game runtime status",
        [](const CommandArguments&) {
            spdlog::info("game status: {}", "running");
            return CommandExecutionStatus::handled;
        });

    if (!registered)
    {
        throw std::runtime_error("failed to register game status command");
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
