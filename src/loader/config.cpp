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
    configuration.config_path = (std::filesystem::path("conf") / (application_name + ".json")).string();
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
    if (options.log_level)
    {
        configuration.log.level = *options.log_level;
    }

    if (options.pid_file)
    {
        configuration.runtime.pid_file = *options.pid_file;
    }

    if (options.log_file)
    {
        configuration.log.file = *options.log_file;
    }

    if (options.error_log_file)
    {
        configuration.log.error_file = *options.error_log_file;
    }

    if (options.daemon.value_or(false))
    {
        configuration.runtime.daemon = true;
        configuration.log.console = false;
    }

    if (options.syslog.value_or(false))
    {
        configuration.log.syslog = true;
    }

    if (options.console)
    {
        configuration.log.console = *options.console;
    }

    if (options.file_log && !*options.file_log)
    {
        configuration.log.file.clear();
    }

    if (options.error_log && !*options.error_log)
    {
        configuration.log.error_file.clear();
    }

    if (options.log_max_size)
    {
        configuration.log.rotate.max_size = *options.log_max_size;
    }

    if (options.log_max_files)
    {
        configuration.log.rotate.max_files = *options.log_max_files;
    }

    if (options.log_rotation_mode)
    {
        configuration.log.rotate.mode = *options.log_rotation_mode;
    }

    if (options.log_rotate_hour)
    {
        configuration.log.rotate.daily_hour = *options.log_rotate_hour;
    }

    if (options.log_rotate_minute)
    {
        configuration.log.rotate.daily_minute = *options.log_rotate_minute;
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
    LoaderConfiguration configuration = BuildDefaultLoaderConfiguration(app);
    configuration.executable_path = std::move(loader.executable_path);
    configuration.arguments = std::move(loader.arguments);
    configuration.verbose = loader.verbose;
    application = app.CreateConfiguration();
    if (!application)
    {
        std::cerr << "failed to create application configuration" << std::endl;
        return false;
    }

    const bool explicit_config_path = options.config_path.has_value();
    if (explicit_config_path)
    {
        configuration.config_path = *options.config_path;
    }

    if (!std::filesystem::exists(configuration.config_path))
    {
        if (explicit_config_path)
        {
            std::cerr << "failed to open config file: " << configuration.config_path << std::endl;
            return false;
        }

        if (configuration.verbose)
        {
            std::cout << "config file not found, skip default path: " << configuration.config_path << std::endl;
        }
        configuration.config_path.clear();
    }
    else
    {
        std::string document;
        std::string error;
        if (!LoadConfigurationDocument(configuration.config_path, document, error))
        {
            std::cerr << error << std::endl;
            return false;
        }

        if (!ApplyLoaderConfiguration(configuration, document, error))
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

    ApplyCliOverrides(configuration, options);

    std::string error;
    if (!ValidateLoaderConfiguration(configuration, error))
    {
        std::cerr << error << std::endl;
        return false;
    }

    loader = std::move(configuration);
    return true;
}
