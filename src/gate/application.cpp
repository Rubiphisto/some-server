#include "application.h"

#include "framework/config/access.h"

#include <spdlog/spdlog.h>

bool GateConfiguration::OverlayFromConfig(const ConfigValue& root, std::string& error)
{
    const ConfigValue* application = root.Find("application");
    if (application == nullptr)
    {
        return true;
    }

    const ConfigValue* listen = application->Find("listen");
    if (listen == nullptr)
    {
        return true;
    }

    if (!config_access::ReadString(*listen, "host", listen_host, error, "application.listen.host"))
    {
        return false;
    }

    if (!config_access::ReadUInt(*listen, "port", listen_port, error, "application.listen.port"))
    {
        return false;
    }

    return true;
}

void Application::Load()
{
    spdlog::info("Application::Configure(listen={}:{})",
                 AppConfig().listen_host,
                 AppConfig().listen_port);
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
