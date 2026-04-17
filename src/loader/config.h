#pragma once

#include "configuration.h"
#include "framework/application/application.h"

struct StartupOptions;

LoaderConfiguration BuildDefaultLoaderConfiguration(const StartupOptions& options, const IApplication& application);
bool LoadConfigurationDocument(const std::string& path, std::string& document, std::string& error);
bool ApplyLoaderConfiguration(LoaderConfiguration& configuration, std::string_view document, std::string& error);
void ApplyCliOverrides(LoaderConfiguration& configuration, const StartupOptions& options);
bool ValidateLoaderConfiguration(const LoaderConfiguration& configuration, std::string& error);
bool ResolveConfiguration(LoaderConfiguration& loader, std::unique_ptr<IApplicationConfiguration>& application, const StartupOptions& options, IApplication& app);
