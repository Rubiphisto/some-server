#include "application.h"

#include <spdlog/spdlog.h>

void Application::Load()
{
    spdlog::info("Application::Configure(listen={}:{})",
                 AppConfig().listen.host,
                 AppConfig().listen.port);
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
