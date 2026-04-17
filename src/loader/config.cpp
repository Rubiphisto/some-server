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

LoaderConfiguration BuildDefaultLoaderConfiguration(const StartupOptions& options, const IApplication& application)
{
    LoaderConfiguration configuration;
    configuration.config_path = ResolveConfigPath(options, application);
    configuration.runtime.pid_file = ResolvePidFilePath(options, application);
    configuration.log.file = ResolveLogFilePath(options, application);
    configuration.log.error_file = ResolveErrorLogFilePath(options, application);
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

bool ApplyLoaderConfiguration(LoaderConfiguration& configuration, std::string_view document, std::string& error)
{
    LoaderConfigurationDocument root{};
    root.loader = configuration;

    if (auto result = glz::read<glz::opts{.error_on_unknown_keys = false}>(root, document))
    {
        error = glz::format_error(result, document);
        return false;
    }

    configuration = std::move(root.loader);
    return true;
}

void ApplyCliOverrides(LoaderConfiguration& configuration, const StartupOptions& options)
{
    if (!options.log_level.empty())
    {
        configuration.log.level = options.log_level;
    }

    if (!options.pid_file.empty())
    {
        configuration.runtime.pid_file = options.pid_file;
    }

    if (!options.log_file.empty())
    {
        configuration.log.file = options.log_file;
    }

    if (!options.error_log_file.empty())
    {
        configuration.log.error_file = options.error_log_file;
    }

    if (options.daemon)
    {
        configuration.runtime.daemon = true;
        configuration.log.console = false;
    }

    if (options.syslog)
    {
        configuration.log.syslog = true;
    }

    if (options.disable_console)
    {
        configuration.log.console = false;
    }

    if (options.disable_file_log)
    {
        configuration.log.file.clear();
    }

    if (options.disable_error_log)
    {
        configuration.log.error_file.clear();
    }

    if (options.log_max_size_explicit)
    {
        configuration.log.rotate.max_size = options.log_max_size;
    }

    if (options.log_max_files_explicit)
    {
        configuration.log.rotate.max_files = options.log_max_files;
    }

    if (!options.log_rotation_mode.empty())
    {
        configuration.log.rotate.mode = options.log_rotation_mode;
    }

    if (options.log_rotate_hour_explicit)
    {
        configuration.log.rotate.daily_hour = options.log_rotate_hour;
    }

    if (options.log_rotate_minute_explicit)
    {
        configuration.log.rotate.daily_minute = options.log_rotate_minute;
    }
}

bool ValidateLoaderConfiguration(const LoaderConfiguration& configuration, std::string& error)
{
    const std::string mode = ToLower(configuration.log.rotate.mode);
    if (mode != "size" && mode != "daily")
    {
        error = "loader.log.rotate.mode must be one of [size, daily]";
        return false;
    }

    if (configuration.log.rotate.max_files == 0)
    {
        error = "loader.log.rotate.max_files must be greater than 0";
        return false;
    }

    if (configuration.log.rotate.daily_hour > 23)
    {
        error = "loader.log.rotate.daily_hour must be in range 0-23";
        return false;
    }

    if (configuration.log.rotate.daily_minute > 59)
    {
        error = "loader.log.rotate.daily_minute must be in range 0-59";
        return false;
    }

    return true;
}

bool ResolveConfiguration(LoaderConfiguration& loader,
                          std::unique_ptr<IApplicationConfiguration>& application,
                          const StartupOptions& options,
                          IApplication& app)
{
    loader = BuildDefaultLoaderConfiguration(options, app);
    application = app.CreateConfiguration();
    if (!application)
    {
        std::cerr << "failed to create application configuration" << std::endl;
        return false;
    }

    if (!std::filesystem::exists(loader.config_path))
    {
        if (!options.config_path.empty())
        {
            std::cerr << "failed to open config file: " << loader.config_path << std::endl;
            return false;
        }

        if (loader.verbose)
        {
            std::cout << "config file not found, skip default path: " << loader.config_path << std::endl;
        }
        loader.config_path.clear();
    }
    else
    {
        std::string document;
        std::string error;
        if (!LoadConfigurationDocument(loader.config_path, document, error))
        {
            std::cerr << error << std::endl;
            return false;
        }

        if (!ApplyLoaderConfiguration(loader, document, error))
        {
            std::cerr << error << std::endl;
            return false;
        }

        if (!application->LoadFromJson(document, error))
        {
            std::cerr << error << std::endl;
            return false;
        }
    }

    ApplyCliOverrides(loader, options);

    std::string error;
    if (!ValidateLoaderConfiguration(loader, error))
    {
        std::cerr << error << std::endl;
        return false;
    }

    return true;
}
