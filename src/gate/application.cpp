#include "application.h"

#include <spdlog/spdlog.h>

void Application::Load()
{
    spdlog::info("Application::Configure(config={}, listen={}:{}, log_file={}, error_log_file={}, log_level={}, daemon={}, args={}, settings={})",
                 Context().config_path,
                 Context().listen.host,
                 Context().listen.port,
                 Context().log.file,
                 Context().log.error_file,
                 Context().log.level,
                 Context().runtime.daemon ? "true" : "false",
                 Context().arguments.size(),
                 Context().settings.size());
    spdlog::debug("Application logging sinks: console={} syslog={} rotate_mode={} rotate_size={} rotate_files={} rotate_at={:02d}:{:02d}",
                  Context().log.console ? "true" : "false",
                  Context().log.syslog ? "true" : "false",
                  Context().log.rotate.mode,
                  Context().log.rotate.max_size,
                  Context().log.rotate.max_files,
                  static_cast<int>(Context().log.rotate.daily_hour),
                  static_cast<int>(Context().log.rotate.daily_minute));
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
