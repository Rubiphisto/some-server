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
    spdlog::debug("Application logging sinks: console={} syslog={} rotate_size={} rotate_files={}",
                  mContext.log_to_console ? "true" : "false",
                  mContext.log_to_syslog ? "true" : "false",
                  mContext.log_max_size,
                  mContext.log_max_files);
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
