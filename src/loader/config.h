#pragma once

#include "configuration.h"
#include "framework/application/application.h"

LoaderConfiguration BuildDefaultLoaderConfiguration(const IApplication& application);
bool ValidateLoaderConfiguration(const LoaderConfiguration& loader_config, std::string& error);
bool LoadConfiguration(LoaderConfiguration& loader_config,
                       const std::string& main_path,
                       const std::string& override_path,
                       std::unique_ptr<IApplicationConfiguration>& application_configuration,
                       IApplication& application);
