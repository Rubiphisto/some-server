#include "application.h"

#include <iostream>

bool Application::Configure(const ApplicationContext& context)
{
    mContext = context;
    return true;
}

void Application::Load()
{
    std::cout << "Application::Configure(config=" << mContext.config_path
              << ", log_level=" << mContext.log_level
              << ", daemon=" << (mContext.daemon ? "true" : "false")
              << ", args=" << mContext.arguments.size()
              << ", settings=" << mContext.settings.size() << ")" << std::endl;
    std::cout << "Application::Load()" << std::endl;
}

void Application::Start()
{
    std::cout << "Application::Start()" << std::endl;
}

void Application::Stop()
{
    std::cout << "Application::Stop()" << std::endl;
}

void Application::Unload()
{
    std::cout << "Application::Unload()" << std::endl;
}
