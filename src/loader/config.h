#pragma once

#include "configuration.h"
#include "framework/application/application.h"

#include <optional>

LoaderConfiguration BuildDefaultLoaderConfiguration(const IApplication& application);
bool LoadConfigurationDocument(const std::string& path, std::string& document, std::string& error);
bool ApplyLoaderConfiguration(LoaderConfiguration& loader_config, std::string_view document, std::string& error);
bool ApplyConfigurationDocument(LoaderConfiguration& loader_config,
                                IApplicationConfiguration& app_config,
                                const std::string& path,
                                std::string& error);
bool ValidateLoaderConfiguration(const LoaderConfiguration& loader_config, std::string& error);
bool ResolveConfiguration(LoaderConfiguration& loader_config,
                          std::unique_ptr<IApplicationConfiguration>& app_config,
                          const std::optional<std::string>& override_config_path,
                          IApplication& app);
