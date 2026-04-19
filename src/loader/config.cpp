#include "config.h"

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
    const std::string application_name = application.GetName();
    configuration.log.file = (std::filesystem::path("logs") / (application_name + ".log")).string();
    configuration.log.error_file = (std::filesystem::path("logs") / (application_name + ".error.log")).string();
    return configuration;
}

namespace
{
bool LoadConfigurationDocument(const std::filesystem::path& file_path, std::string& document, std::string& error)
{
    const std::string extension = ToLower(file_path.extension().string());
    if (!extension.empty() && extension != ".json")
    {
        error = "unsupported config format: " + file_path.string();
        return false;
    }

    std::ifstream input(file_path);
    if (!input)
    {
        error = "failed to open config file: " + file_path.string();
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

bool ApplyConfigurationFile(LoaderConfiguration& loader_config,
                            IApplicationConfiguration& application_configuration,
                            const std::filesystem::path& file_path,
                            std::string& error)
{
    std::string document;
    if (!LoadConfigurationDocument(file_path, document, error))
    {
        return false;
    }

    if (!ApplyLoaderConfiguration(loader_config, document, error))
    {
        return false;
    }

    if (!application_configuration.LoadFromJson(document, error))
    {
        return false;
    }

    return true;
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

bool LoadConfiguration(LoaderConfiguration& loader_config,
                       const std::string& main_path,
                       const std::string& override_path,
                       std::unique_ptr<IApplicationConfiguration>& application_configuration,
                       IApplication& application)
{
    application_configuration = application.CreateConfiguration();
    if (!application_configuration)
    {
        std::cerr << "failed to create application configuration" << std::endl;
        return false;
    }

    std::string error;
    if (!std::filesystem::exists(main_path))
    {
        std::cerr << "failed to open main config file: " << main_path << std::endl;
        return false;
    }

    if (!ApplyConfigurationFile(loader_config, *application_configuration, main_path, error))
    {
        std::cerr << error << std::endl;
        return false;
    }

    if (!override_path.empty())
    {
        if (std::filesystem::exists(override_path) &&
            !ApplyConfigurationFile(loader_config, *application_configuration, override_path, error))
        {
            std::cerr << error << std::endl;
            return false;
        }
    }

    if (!ValidateLoaderConfiguration(loader_config, error))
    {
        std::cerr << error << std::endl;
        return false;
    }

    return true;
}
