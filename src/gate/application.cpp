#include "application.h"

#include <spdlog/spdlog.h>

bool Application::Configure(const ApplicationContext& context)
{
    mContext = context;
    return true;
}

void Application::Load()
{
    spdlog::info("Application::Configure(config={}, log_file={}, log_level={}, daemon={}, args={}, settings={})",
                 mContext.config_path,
                 mContext.log_file,
                 mContext.log_level,
                 mContext.daemon ? "true" : "false",
                 mContext.arguments.size(),
                 mContext.settings.size());
    spdlog::info("Application::Load()");
}

void Application::Start()
{
    spdlog::info("Application::Start()");
}

void Application::Stop()
{
    spdlog::info("Application::Stop()");
}

void Application::Unload()
{
    spdlog::info("Application::Unload()");
}
