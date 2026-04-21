#include "runtime_command_service.h"

#include <spdlog/spdlog.h>

#include <stdexcept>

namespace
{
    constexpr std::int32_t kRuntimeCommandBatch = 10;
}

RuntimeCommandService::RuntimeCommandService(IApplicationRuntime& runtime)
    : ServiceBase("gate.runtime_command", kRuntimeCommandBatch), mRuntime(runtime)
{
}

LifecycleTask RuntimeCommandService::Load()
{
    const bool registered = mRuntime.RegisterCommand(
        "status",
        "Show gate runtime status",
        [this](const CommandArguments&) {
            spdlog::info("gate status: {}", mRunning ? "running" : "stopped");
            return CommandExecutionStatus::handled;
        });

    if (!registered)
    {
        throw std::runtime_error("failed to register gate status command");
    }

    spdlog::info("RuntimeCommandService::Load()");
    return LifecycleTask::Completed();
}

LifecycleTask RuntimeCommandService::Start()
{
    mRunning = true;
    spdlog::info("RuntimeCommandService::Start()");
    return LifecycleTask::Completed();
}

LifecycleTask RuntimeCommandService::Stop()
{
    mRunning = false;
    spdlog::info("RuntimeCommandService::Stop()");
    return LifecycleTask::Completed();
}

LifecycleTask RuntimeCommandService::Unload()
{
    spdlog::info("RuntimeCommandService::Unload()");
    return LifecycleTask::Completed();
}
