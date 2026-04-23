#include "application.h"

#include <spdlog/spdlog.h>

#include <stdexcept>

void Application::RegisterRuntimeCommands()
{
    const bool registered = Runtime().RegisterCommand(
        "ipc_status",
        "Show relay IPC bootstrap status",
        [](const CommandArguments&) {
            // Relay is introduced as a normal application shell first; richer IPC
            // runtime state will be wired in as discovery/routing layers land.
            spdlog::info("relay ipc status: {}", "bootstrap");
            return CommandExecutionStatus::handled;
        });

    if (!registered)
    {
        throw std::runtime_error("failed to register relay ipc status command");
    }
}
