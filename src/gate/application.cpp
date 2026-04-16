#include "application.h"

#include <spdlog/spdlog.h>

void Application::Load()
{
    spdlog::info("Application::Configure(config={}, listen={}:{}, log_file={}, error_log_file={}, log_level={}, daemon={}, args={}, settings={})",
                 Context().config_path,
                 Context().listen_host,
                 Context().listen_port,
                 Context().log_file,
                 Context().error_log_file,
                 Context().log_level,
                 Context().daemon ? "true" : "false",
                 Context().arguments.size(),
                 Context().settings.size());
    spdlog::debug("Application logging sinks: console={} syslog={} rotate_mode={} rotate_size={} rotate_files={} rotate_at={:02d}:{:02d}",
                  Context().log_to_console ? "true" : "false",
                  Context().log_to_syslog ? "true" : "false",
                  Context().log_rotation_mode,
                  Context().log_max_size,
                  Context().log_max_files,
                  static_cast<int>(Context().log_rotate_hour),
                  static_cast<int>(Context().log_rotate_minute));
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
