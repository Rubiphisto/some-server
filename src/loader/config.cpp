#include "config.h"

#include <glaze/glaze.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace
{
    std::string ToLower(std::string text)
    {
        for (char& ch : text)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return text;
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

bool LoadConfigurationValue(const std::string& document, glz::generic& value, std::string& error)
{
    auto result = glz::read_json<glz::generic>(document);
    if (!result)
    {
        error = glz::format_error(result.error(), document);
        return false;
    }

    value = std::move(*result);
    return true;
}

void MergeConfigurationValue(glz::generic& base_value, const glz::generic& override_value)
{
    if (base_value.is_object() && override_value.is_object())
    {
        for (const auto& [key, child_value] : override_value.get_object())
        {
            if (base_value.contains(key))
            {
                MergeConfigurationValue(base_value[key], child_value);
            }
            else
            {
                base_value[key] = child_value;
            }
        }
        return;
    }

    base_value = override_value;
}

bool MergeConfigurationFile(glz::generic& configuration_value,
                            const std::filesystem::path& file_path,
                            std::string& error)
{
    std::string document;
    if (!LoadConfigurationDocument(file_path, document, error))
    {
        return false;
    }

    glz::generic file_value;
    if (!LoadConfigurationValue(document, file_value, error))
    {
        return false;
    }

    MergeConfigurationValue(configuration_value, file_value);
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

    glz::generic configuration_value;
    if (!MergeConfigurationFile(configuration_value, main_path, error))
    {
        std::cerr << error << std::endl;
        return false;
    }

    if (!override_path.empty())
    {
        if (std::filesystem::exists(override_path) &&
            !MergeConfigurationFile(configuration_value, override_path, error))
        {
            std::cerr << error << std::endl;
            return false;
        }
    }

    const auto document = configuration_value.dump();
    if (!document)
    {
        error = glz::format_error(document.error());
        std::cerr << error << std::endl;
        return false;
    }

    if (auto result = glz::read<glz::opts{.error_on_unknown_keys = false}>(loader_config, *document))
    {
        error = glz::format_error(result, *document);
        std::cerr << error << std::endl;
        return false;
    }

    const std::string application_name = application.GetName();
    if (configuration_value.contains(application_name) &&
        !application_configuration->LoadFromGeneric(configuration_value[application_name], error))
    {
        std::cerr << error << std::endl;
        return false;
    }

    if (!ValidateLoaderConfiguration(loader_config, error))
    {
        std::cerr << error << std::endl;
        return false;
    }

    return true;
}
