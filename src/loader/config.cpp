#include "config.h"

#include "options.h"

#include <yaml-cpp/yaml.h>

#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <unordered_map>

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

    std::optional<std::size_t> ParseUnsigned(std::string value)
    {
        if (value.empty())
        {
            return std::nullopt;
        }

        try
        {
            std::size_t processed = 0;
            const auto parsed = std::stoull(value, &processed, 10);
            if (processed != value.size())
            {
                return std::nullopt;
            }
            return static_cast<std::size_t>(parsed);
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
    }

    std::optional<std::size_t> ParseSize(std::string value)
    {
        if (value.empty())
        {
            return std::nullopt;
        }

        std::size_t split = value.size();
        while (split > 0 && std::isalpha(static_cast<unsigned char>(value[split - 1])))
        {
            --split;
        }

        const auto base_value = ParseUnsigned(value.substr(0, split));
        if (!base_value.has_value())
        {
            return std::nullopt;
        }

        const std::string suffix = ToLower(value.substr(split));
        std::size_t multiplier = 1;
        if (suffix.empty() || suffix == "b")
        {
            multiplier = 1;
        }
        else if (suffix == "k" || suffix == "kb")
        {
            multiplier = 1024;
        }
        else if (suffix == "m" || suffix == "mb")
        {
            multiplier = 1024 * 1024;
        }
        else if (suffix == "g" || suffix == "gb")
        {
            multiplier = 1024 * 1024 * 1024ULL;
        }
        else
        {
            return std::nullopt;
        }

        return base_value.value() * multiplier;
    }

    std::string JoinPath(std::string_view prefix, std::string_view child)
    {
        if (prefix.empty())
        {
            return std::string(child);
        }
        return std::string(prefix) + "." + std::string(child);
    }

    std::string NodeAsSettingValue(const YAML::Node& node)
    {
        if (!node || node.IsNull())
        {
            return {};
        }

        if (node.IsScalar())
        {
            return node.Scalar();
        }

        YAML::Emitter emitter;
        emitter << node;
        return emitter.c_str();
    }

    void FlattenSettings(const YAML::Node& node,
                         std::string_view prefix,
                         std::unordered_map<std::string, std::string>& settings)
    {
        if (!node)
        {
            return;
        }

        if (node.IsMap())
        {
            for (const auto& item : node)
            {
                if (!item.first.IsScalar())
                {
                    continue;
                }

                const std::string child_path = JoinPath(prefix, item.first.Scalar());
                FlattenSettings(item.second, child_path, settings);
            }
            return;
        }

        if (prefix.empty())
        {
            return;
        }

        settings[std::string(prefix)] = NodeAsSettingValue(node);
    }

    bool ReportTypeError(std::string_view path, std::string_view expected)
    {
        std::cerr << "config error at " << path << ": expected " << expected << std::endl;
        return false;
    }

    bool ApplyString(const YAML::Node& node, std::string_view path, std::string& target)
    {
        if (!node)
        {
            return true;
        }
        if (!node.IsScalar())
        {
            return ReportTypeError(path, "scalar");
        }

        target = node.Scalar();
        return true;
    }

    bool ApplyBool(const YAML::Node& node, std::string_view path, bool& target)
    {
        if (!node)
        {
            return true;
        }
        if (!node.IsScalar())
        {
            return ReportTypeError(path, "bool");
        }

        try
        {
            target = node.as<bool>();
            return true;
        }
        catch (const YAML::Exception&)
        {
            return ReportTypeError(path, "bool");
        }
    }

    bool ApplyUnsigned(const YAML::Node& node, std::string_view path, std::size_t& target)
    {
        if (!node)
        {
            return true;
        }
        if (!node.IsScalar())
        {
            return ReportTypeError(path, "unsigned integer");
        }

        try
        {
            target = node.as<std::size_t>();
            return true;
        }
        catch (const YAML::Exception&)
        {
            return ReportTypeError(path, "unsigned integer");
        }
    }

    bool ApplySizeValue(const YAML::Node& node, std::string_view path, std::size_t& target)
    {
        if (!node)
        {
            return true;
        }
        if (!node.IsScalar())
        {
            return ReportTypeError(path, "size scalar");
        }

        const auto parsed = ParseSize(node.Scalar());
        if (!parsed.has_value())
        {
            std::cerr << "config error at " << path << ": invalid size value '" << node.Scalar() << "'" << std::endl;
            return false;
        }

        target = parsed.value();
        return true;
    }

    void SetSetting(std::unordered_map<std::string, std::string>& settings,
                    std::string_view key,
                    std::string value)
    {
        settings[std::string(key)] = std::move(value);
    }

    void SetSetting(std::unordered_map<std::string, std::string>& settings,
                    std::string_view key,
                    const char* value)
    {
        settings[std::string(key)] = value;
    }

}

ApplicationContext BuildDefaultContext(const StartupOptions& options, const IApplication& application)
{
    ApplicationContext context;
    context.config_path = ResolveConfigPath(options, application);
    context.runtime.pid_file = ResolvePidFilePath(options, application);
    context.log.file = ResolveLogFilePath(options, application);
    context.log.error_file = ResolveErrorLogFilePath(options, application);
    return context;
}

bool LoadYamlIntoContext(ApplicationContext& context, bool config_path_explicit, bool verbose)
{
    if (context.config_path.empty())
    {
        return true;
    }

    if (!std::filesystem::exists(context.config_path))
    {
        if (config_path_explicit)
        {
            std::cerr << "failed to open config file: " << context.config_path << std::endl;
            return false;
        }

        if (verbose)
        {
            std::cout << "config file not found, skip default path: " << context.config_path << std::endl;
        }
        context.config_path.clear();
        return true;
    }

    YAML::Node root;
    try
    {
        root = YAML::LoadFile(context.config_path);
    }
    catch (const YAML::BadFile&)
    {
        std::cerr << "failed to open config file: " << context.config_path << std::endl;
        return false;
    }
    catch (const YAML::ParserException& ex)
    {
        std::cerr << "failed to parse config file " << context.config_path << ": " << ex.what() << std::endl;
        return false;
    }
    catch (const YAML::Exception& ex)
    {
        std::cerr << "failed to read config file " << context.config_path << ": " << ex.what() << std::endl;
        return false;
    }

    if (!root || root.IsNull())
    {
        return true;
    }

    if (!root.IsMap())
    {
        std::cerr << "config error: root node must be a map" << std::endl;
        return false;
    }

    FlattenSettings(root, {}, context.settings);

    if (!ApplyString(root["listen"]["host"], "listen.host", context.listen.host) ||
        !ApplyUnsigned(root["listen"]["port"], "listen.port", context.listen.port) ||
        !ApplyString(root["log"]["level"], "log.level", context.log.level) ||
        !ApplyString(root["log"]["file"], "log.file", context.log.file) ||
        !ApplyString(root["log"]["error_file"], "log.error_file", context.log.error_file) ||
        !ApplyBool(root["log"]["console"], "log.console", context.log.console) ||
        !ApplyBool(root["log"]["syslog"], "log.syslog", context.log.syslog) ||
        !ApplyString(root["log"]["rotate"]["mode"], "log.rotate.mode", context.log.rotate.mode) ||
        !ApplySizeValue(root["log"]["rotate"]["max_size"], "log.rotate.max_size", context.log.rotate.max_size) ||
        !ApplyUnsigned(root["log"]["rotate"]["max_files"], "log.rotate.max_files", context.log.rotate.max_files) ||
        !ApplyUnsigned(root["log"]["rotate"]["daily_hour"], "log.rotate.daily_hour", context.log.rotate.daily_hour) ||
        !ApplyUnsigned(root["log"]["rotate"]["daily_minute"], "log.rotate.daily_minute", context.log.rotate.daily_minute) ||
        !ApplyBool(root["runtime"]["daemon"], "runtime.daemon", context.runtime.daemon) ||
        !ApplyString(root["runtime"]["pid_file"], "runtime.pid_file", context.runtime.pid_file))
    {
        return false;
    }

    return true;
}

void ApplyCliOverrides(ApplicationContext& context, const StartupOptions& options)
{
    if (!options.log_level.empty())
    {
        context.log.level = options.log_level;
        SetSetting(context.settings, "log.level", options.log_level);
    }

    if (!options.pid_file.empty())
    {
        context.runtime.pid_file = options.pid_file;
        SetSetting(context.settings, "runtime.pid_file", options.pid_file);
    }

    if (!options.log_file.empty())
    {
        context.log.file = options.log_file;
        SetSetting(context.settings, "log.file", options.log_file);
    }

    if (!options.error_log_file.empty())
    {
        context.log.error_file = options.error_log_file;
        SetSetting(context.settings, "log.error_file", options.error_log_file);
    }

    if (options.daemon)
    {
        context.runtime.daemon = true;
        context.log.console = false;
        SetSetting(context.settings, "runtime.daemon", "true");
        SetSetting(context.settings, "log.console", "false");
    }

    if (options.syslog)
    {
        context.log.syslog = true;
        SetSetting(context.settings, "log.syslog", "true");
    }

    if (options.disable_console)
    {
        context.log.console = false;
        SetSetting(context.settings, "log.console", "false");
    }

    if (options.disable_file_log)
    {
        context.log.file.clear();
        SetSetting(context.settings, "log.file", "");
    }

    if (options.disable_error_log)
    {
        context.log.error_file.clear();
        SetSetting(context.settings, "log.error_file", "");
    }

    if (options.log_max_size_explicit)
    {
        context.log.rotate.max_size = options.log_max_size;
        SetSetting(context.settings, "log.rotate.max_size", std::to_string(options.log_max_size));
    }

    if (options.log_max_files_explicit)
    {
        context.log.rotate.max_files = options.log_max_files;
        SetSetting(context.settings, "log.rotate.max_files", std::to_string(options.log_max_files));
    }

    if (!options.log_rotation_mode.empty())
    {
        context.log.rotate.mode = options.log_rotation_mode;
        SetSetting(context.settings, "log.rotate.mode", options.log_rotation_mode);
    }

    if (options.log_rotate_hour_explicit)
    {
        context.log.rotate.daily_hour = options.log_rotate_hour;
        SetSetting(context.settings, "log.rotate.daily_hour", std::to_string(options.log_rotate_hour));
    }

    if (options.log_rotate_minute_explicit)
    {
        context.log.rotate.daily_minute = options.log_rotate_minute;
        SetSetting(context.settings, "log.rotate.daily_minute", std::to_string(options.log_rotate_minute));
    }
}

bool ValidateContext(const ApplicationContext& context)
{
    const std::string mode = ToLower(context.log.rotate.mode);
    if (mode != "size" && mode != "daily")
    {
        std::cerr << "config error at log.rotate.mode: expected one of [size, daily]" << std::endl;
        return false;
    }

    if (context.listen.port == 0 || context.listen.port > 65535)
    {
        std::cerr << "config error at listen.port: must be in range 1-65535" << std::endl;
        return false;
    }

    if (context.log.rotate.max_files == 0)
    {
        std::cerr << "config error at log.rotate.max_files: must be greater than 0" << std::endl;
        return false;
    }

    if (context.log.rotate.daily_hour > 23)
    {
        std::cerr << "config error at log.rotate.daily_hour: must be in range 0-23" << std::endl;
        return false;
    }

    if (context.log.rotate.daily_minute > 59)
    {
        std::cerr << "config error at log.rotate.daily_minute: must be in range 0-59" << std::endl;
        return false;
    }

    return true;
}

bool ResolveConfiguration(ApplicationContext& context, const StartupOptions& options, const IApplication& application)
{
    context = BuildDefaultContext(options, application);

    if (!LoadYamlIntoContext(context, options.config_path_explicit, context.verbose))
    {
        return false;
    }

    ApplyCliOverrides(context, options);
    if (!ValidateContext(context))
    {
        return false;
    }
    return true;
}
