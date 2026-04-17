#include "config.h"

#include "framework/config/access.h"
#include "options.h"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <iostream>
#include <string_view>

namespace
{
    ConfigValue ConvertYamlNode(const YAML::Node& node)
    {
        if (!node || node.IsNull())
        {
            return {};
        }

        if (node.IsMap())
        {
            ConfigValue::Object object;
            for (const auto& item : node)
            {
                if (item.first.IsScalar())
                {
                    object.emplace(item.first.Scalar(), ConvertYamlNode(item.second));
                }
            }
            return ConfigValue(std::move(object));
        }

        if (node.IsSequence())
        {
            ConfigValue::Array array;
            array.reserve(node.size());
            for (const auto& child : node)
            {
                array.push_back(ConvertYamlNode(child));
            }
            return ConfigValue(std::move(array));
        }

        if (node.IsScalar())
        {
            try
            {
                return ConfigValue(node.as<bool>());
            }
            catch (const YAML::Exception&)
            {
            }

            try
            {
                return ConfigValue(static_cast<std::uint64_t>(node.as<std::uint64_t>()));
            }
            catch (const YAML::Exception&)
            {
            }

            return ConfigValue(node.Scalar());
        }

        return {};
    }

    void FlattenSettings(const ConfigValue& node,
                         std::string_view prefix,
                         std::unordered_map<std::string, std::string>& settings)
    {
        if (const auto* object = node.AsObject(); object != nullptr)
        {
            for (const auto& [key, value] : *object)
            {
                const std::string child = prefix.empty() ? key : std::string(prefix) + "." + key;
                FlattenSettings(value, child, settings);
            }
            return;
        }

        if (prefix.empty())
        {
            return;
        }

        if (const auto* text = node.AsString(); text != nullptr)
        {
            settings[std::string(prefix)] = *text;
        }
        else if (const auto* flag = node.AsBool(); flag != nullptr)
        {
            settings[std::string(prefix)] = *flag ? "true" : "false";
        }
        else if (const auto* integer = node.AsUInt(); integer != nullptr)
        {
            settings[std::string(prefix)] = std::to_string(*integer);
        }
    }

    void SetSetting(std::unordered_map<std::string, std::string>& settings,
                    std::string_view key,
                    const std::string& value)
    {
        settings[std::string(key)] = value;
    }

    void SetSetting(std::unordered_map<std::string, std::string>& settings,
                    std::string_view key,
                    const char* value)
    {
        settings[std::string(key)] = value;
    }
}

bool LoaderRuntimeConfiguration::OverlayFromConfig(const ConfigValue& root, std::string& error)
{
    return config_access::ReadBool(root, "daemon", daemon, error, "loader.runtime.daemon") &&
           config_access::ReadString(root, "pid_file", pid_file, error, "loader.runtime.pid_file");
}

bool LoaderLogRotationConfiguration::OverlayFromConfig(const ConfigValue& root, std::string& error)
{
    return config_access::ReadString(root, "mode", mode, error, "loader.log.rotate.mode") &&
           config_access::ReadSize(root, "max_size", max_size, error, "loader.log.rotate.max_size") &&
           config_access::ReadUInt(root, "max_files", max_files, error, "loader.log.rotate.max_files") &&
           config_access::ReadUInt(root, "daily_hour", daily_hour, error, "loader.log.rotate.daily_hour") &&
           config_access::ReadUInt(root, "daily_minute", daily_minute, error, "loader.log.rotate.daily_minute");
}

bool LoaderLogConfiguration::OverlayFromConfig(const ConfigValue& root, std::string& error)
{
    if (!config_access::ReadString(root, "file", file, error, "loader.log.file") ||
        !config_access::ReadString(root, "error_file", error_file, error, "loader.log.error_file") ||
        !config_access::ReadString(root, "level", level, error, "loader.log.level") ||
        !config_access::ReadBool(root, "console", console, error, "loader.log.console") ||
        !config_access::ReadBool(root, "syslog", syslog, error, "loader.log.syslog"))
    {
        return false;
    }

    const ConfigValue* rotate_root = root.Find("rotate");
    return rotate_root == nullptr || rotate.OverlayFromConfig(*rotate_root, error);
}

bool LoaderConfiguration::OverlayFromConfig(const ConfigValue& root, std::string& error)
{
    FlattenSettings(root, "loader", settings);

    const ConfigValue* runtime_root = root.Find("runtime");
    if (runtime_root != nullptr && !runtime.OverlayFromConfig(*runtime_root, error))
    {
        return false;
    }

    const ConfigValue* log_root = root.Find("log");
    return log_root == nullptr || log.OverlayFromConfig(*log_root, error);
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

bool LoadConfigurationDocument(const std::string& path, ConfigValue& document, std::string& error)
{
    const std::string extension = config_access::ToLower(std::filesystem::path(path).extension().string());
    if (!extension.empty() && extension != ".yaml" && extension != ".yml" && extension != ".json")
    {
        error = "unsupported config format: " + path;
        return false;
    }

    try
    {
        document = ConvertYamlNode(YAML::LoadFile(path));
        return true;
    }
    catch (const YAML::BadFile&)
    {
        error = "failed to open config file: " + path;
        return false;
    }
    catch (const YAML::ParserException& ex)
    {
        error = "failed to parse config file " + path + ": " + ex.what();
        return false;
    }
    catch (const YAML::Exception& ex)
    {
        error = "failed to read config file " + path + ": " + ex.what();
        return false;
    }
}

bool ApplyLoaderConfiguration(LoaderConfiguration& configuration, const ConfigValue& document, std::string& error)
{
    const ConfigValue* loader = document.Find("loader");
    return loader == nullptr || configuration.OverlayFromConfig(*loader, error);
}

void ApplyCliOverrides(LoaderConfiguration& configuration, const StartupOptions& options)
{
    if (!options.log_level.empty())
    {
        configuration.log.level = options.log_level;
        SetSetting(configuration.settings, "loader.log.level", options.log_level);
    }

    if (!options.pid_file.empty())
    {
        configuration.runtime.pid_file = options.pid_file;
        SetSetting(configuration.settings, "loader.runtime.pid_file", options.pid_file);
    }

    if (!options.log_file.empty())
    {
        configuration.log.file = options.log_file;
        SetSetting(configuration.settings, "loader.log.file", options.log_file);
    }

    if (!options.error_log_file.empty())
    {
        configuration.log.error_file = options.error_log_file;
        SetSetting(configuration.settings, "loader.log.error_file", options.error_log_file);
    }

    if (options.daemon)
    {
        configuration.runtime.daemon = true;
        configuration.log.console = false;
        SetSetting(configuration.settings, "loader.runtime.daemon", "true");
        SetSetting(configuration.settings, "loader.log.console", "false");
    }

    if (options.syslog)
    {
        configuration.log.syslog = true;
        SetSetting(configuration.settings, "loader.log.syslog", "true");
    }

    if (options.disable_console)
    {
        configuration.log.console = false;
        SetSetting(configuration.settings, "loader.log.console", "false");
    }

    if (options.disable_file_log)
    {
        configuration.log.file.clear();
        SetSetting(configuration.settings, "loader.log.file", "");
    }

    if (options.disable_error_log)
    {
        configuration.log.error_file.clear();
        SetSetting(configuration.settings, "loader.log.error_file", "");
    }

    if (options.log_max_size_explicit)
    {
        configuration.log.rotate.max_size = options.log_max_size;
        SetSetting(configuration.settings, "loader.log.rotate.max_size", std::to_string(options.log_max_size));
    }

    if (options.log_max_files_explicit)
    {
        configuration.log.rotate.max_files = options.log_max_files;
        SetSetting(configuration.settings, "loader.log.rotate.max_files", std::to_string(options.log_max_files));
    }

    if (!options.log_rotation_mode.empty())
    {
        configuration.log.rotate.mode = options.log_rotation_mode;
        SetSetting(configuration.settings, "loader.log.rotate.mode", options.log_rotation_mode);
    }

    if (options.log_rotate_hour_explicit)
    {
        configuration.log.rotate.daily_hour = options.log_rotate_hour;
        SetSetting(configuration.settings, "loader.log.rotate.daily_hour", std::to_string(options.log_rotate_hour));
    }

    if (options.log_rotate_minute_explicit)
    {
        configuration.log.rotate.daily_minute = options.log_rotate_minute;
        SetSetting(configuration.settings, "loader.log.rotate.daily_minute", std::to_string(options.log_rotate_minute));
    }
}

bool ValidateLoaderConfiguration(const LoaderConfiguration& configuration, std::string& error)
{
    const std::string mode = config_access::ToLower(configuration.log.rotate.mode);
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
        ConfigValue document;
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

        if (!application->OverlayFromConfig(document, error))
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
