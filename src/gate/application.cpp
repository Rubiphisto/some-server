#include "application.h"

#include <spdlog/spdlog.h>

bool Application::Configure(const ApplicationContext& context)
{
    mContext = context;
    return true;
}

void Application::Load()
{
    spdlog::info("Application::Configure(config={}, log_file={}, error_log_file={}, log_level={}, daemon={}, args={}, settings={})",
                 mContext.config_path,
                 mContext.log_file,
                 mContext.error_log_file,
                 mContext.log_level,
                 mContext.daemon ? "true" : "false",
                 mContext.arguments.size(),
                 mContext.settings.size());
    spdlog::debug("Application logging sinks: console={} syslog={} rotate_mode={} rotate_size={} rotate_files={} rotate_at={:02d}:{:02d}",
                  mContext.log_to_console ? "true" : "false",
                  mContext.log_to_syslog ? "true" : "false",
                  mContext.log_rotation_mode,
                  mContext.log_max_size,
                  mContext.log_max_files,
                  static_cast<int>(mContext.log_rotate_hour),
                  static_cast<int>(mContext.log_rotate_minute));
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
