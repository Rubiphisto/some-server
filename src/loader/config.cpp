#include "config.h"

#include "options.h"

#include <glaze/glaze.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace
{
    std::string ToLower(std::string value)
    {
        for (char& ch : value)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    }
}

LoaderConfiguration BuildDefaultLoaderConfiguration(const IApplication& application)
{
    LoaderConfiguration configuration;
    const std::string application_name = Narrow(application.GetName());
    configuration.config_path =
        (std::filesystem::current_path() / "conf" / (application_name + ".json")).lexically_normal().string();
    configuration.runtime.pid_file = (std::filesystem::path("run") / (application_name + ".pid")).string();
    configuration.log.file = (std::filesystem::path("logs") / (application_name + ".log")).string();
    configuration.log.error_file = (std::filesystem::path("logs") / (application_name + ".error.log")).string();
    return configuration;
}

bool LoadConfigurationDocument(const std::string& path, std::string& document, std::string& error)
{
    const std::string extension = ToLower(std::filesystem::path(path).extension().string());
    if (!extension.empty() && extension != ".json")
    {
        error = "unsupported config format: " + path;
        return false;
    }

    std::ifstream input(path);
    if (!input)
    {
        error = "failed to open config file: " + path;
        return false;
    }

    document.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

bool ApplyLoaderConfiguration(LoaderConfiguration& loader_config, std::string_view document, std::string& error)
{
    LoaderConfigurationDocument root{};
    root.loader = loader_config;

    if (auto result = glz::read<glz::opts{.error_on_unknown_keys = false}>(root, document))
    {
        error = glz::format_error(result, document);
        return false;
    }

    loader_config = std::move(root.loader);
    return true;
}

bool ApplyConfigurationDocument(LoaderConfiguration& loader_config,
                                IApplicationConfiguration& app_config,
                                const std::string& path,
                                std::string& error)
{
    std::string document;
    if (!LoadConfigurationDocument(path, document, error))
    {
        return false;
    }

    if (!ApplyLoaderConfiguration(loader_config, document, error))
    {
        return false;
    }

    if (!app_config.LoadFromJson(document, error))
    {
        return false;
    }

    return true;
}

void ApplyCliOverrides(LoaderConfiguration& loader_config, const StartupOptions& options)
{
    if (options.pid_file)
    {
        loader_config.runtime.pid_file = *options.pid_file;
    }

    if (options.daemon.value_or(false))
    {
        loader_config.runtime.daemon = true;
        loader_config.log.console = false;
    }
}

bool ValidateLoaderConfiguration(const LoaderConfiguration& loader_config, std::string& error)
{
    const std::string mode = ToLower(loader_config.log.rotate.mode);
    if (mode != "size" && mode != "daily")
    {
        error = "loader.log.rotate.mode must be one of [size, daily]";
        return false;
    }

    if (loader_config.log.rotate.max_files == 0)
    {
        error = "loader.log.rotate.max_files must be greater than 0";
        return false;
    }

    if (loader_config.log.rotate.daily_hour > 23)
    {
        error = "loader.log.rotate.daily_hour must be in range 0-23";
        return false;
    }

    if (loader_config.log.rotate.daily_minute > 59)
    {
        error = "loader.log.rotate.daily_minute must be in range 0-59";
        return false;
    }

    return true;
}

bool ResolveConfiguration(LoaderConfiguration& loader_config,
                          std::unique_ptr<IApplicationConfiguration>& app_config,
                          const StartupOptions& options,
                          IApplication& app)
{
    LoaderConfiguration resolved_loader_config = BuildDefaultLoaderConfiguration(app);
    resolved_loader_config.executable_path = std::move(loader_config.executable_path);
    resolved_loader_config.arguments = std::move(loader_config.arguments);
    resolved_loader_config.verbose = loader_config.verbose;
    app_config = app.CreateConfiguration();
    if (!app_config)
    {
        std::cerr << "failed to create application configuration" << std::endl;
        return false;
    }

    std::string error;
    if (!std::filesystem::exists(resolved_loader_config.config_path))
    {
        std::cerr << "failed to open main config file: " << resolved_loader_config.config_path << std::endl;
        return false;
    }

    if (!ApplyConfigurationDocument(resolved_loader_config, *app_config, resolved_loader_config.config_path, error))
    {
        std::cerr << error << std::endl;
        return false;
    }

    if (options.config_path)
    {
        resolved_loader_config.override_config_path = *options.config_path;
        if (!ApplyConfigurationDocument(
                resolved_loader_config, *app_config, resolved_loader_config.override_config_path, error))
        {
            std::cerr << error << std::endl;
            return false;
        }
    }

    ApplyCliOverrides(resolved_loader_config, options);

    if (!ValidateLoaderConfiguration(resolved_loader_config, error))
    {
        std::cerr << error << std::endl;
        return false;
    }

    loader_config = std::move(resolved_loader_config);
    return true;
}
