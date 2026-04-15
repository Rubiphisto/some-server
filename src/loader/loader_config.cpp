#include "loader_config.h"

#include <cctype>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>

namespace
{
    std::string Trim(std::string_view value)
    {
        const auto begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string_view::npos)
        {
            return {};
        }

        const auto end = value.find_last_not_of(" \t\r\n");
        return std::string(value.substr(begin, end - begin + 1));
    }

    std::optional<bool> ParseBool(std::string value)
    {
        for (char& ch : value)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }

        if (value == "1" || value == "true" || value == "yes" || value == "on")
        {
            return true;
        }

        if (value == "0" || value == "false" || value == "no" || value == "off")
        {
            return false;
        }

        return std::nullopt;
    }

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
        value = Trim(value);
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
        value = Trim(value);
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
}

bool LoadConfiguration(ApplicationContext& context, bool config_path_explicit)
{
    if (context.config_path.empty())
    {
        return true;
    }

    std::ifstream config_stream(context.config_path);
    if (!config_stream.is_open())
    {
        if (config_path_explicit)
        {
            std::cerr << "failed to open config file: " << context.config_path << std::endl;
            return false;
        }

        if (context.verbose)
        {
            std::cout << "config file not found, skip default path: " << context.config_path << std::endl;
        }
        context.config_path.clear();
        return true;
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(config_stream, line))
    {
        ++line_number;
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
        {
            continue;
        }

        const auto separator = trimmed.find('=');
        if (separator == std::string::npos)
        {
            std::cerr << "invalid config entry at line " << line_number << ": " << trimmed << std::endl;
            return false;
        }

        const std::string key = Trim(std::string_view(trimmed).substr(0, separator));
        const std::string value = Trim(std::string_view(trimmed).substr(separator + 1));
        if (key.empty())
        {
            std::cerr << "empty config key at line " << line_number << std::endl;
            return false;
        }
        context.settings[key] = value;
    }

    if (const auto it = context.settings.find("log.level"); it != context.settings.end() && !it->second.empty())
    {
        context.log_level = it->second;
    }

    if (const auto it = context.settings.find("log.file"); it != context.settings.end() && !it->second.empty())
    {
        context.log_file = it->second;
    }

    if (const auto it = context.settings.find("runtime.pid_file"); it != context.settings.end() && !it->second.empty())
    {
        context.pid_file = it->second;
    }

    if (const auto it = context.settings.find("log.error_file"); it != context.settings.end())
    {
        context.error_log_file = it->second;
    }

    if (const auto it = context.settings.find("log.rotate.mode"); it != context.settings.end() && !it->second.empty())
    {
        const auto mode = ToLower(it->second);
        if (mode != "size" && mode != "daily")
        {
            std::cerr << "invalid log.rotate.mode value: " << it->second << std::endl;
            return false;
        }
        context.log_rotation_mode = mode;
    }

    if (const auto it = context.settings.find("log.console"); it != context.settings.end())
    {
        const auto value = ParseBool(it->second);
        if (!value.has_value())
        {
            std::cerr << "invalid log.console value: " << it->second << std::endl;
            return false;
        }
        context.log_to_console = value.value();
    }

    if (const auto it = context.settings.find("log.syslog"); it != context.settings.end())
    {
        const auto value = ParseBool(it->second);
        if (!value.has_value())
        {
            std::cerr << "invalid log.syslog value: " << it->second << std::endl;
            return false;
        }
        context.log_to_syslog = value.value();
    }

    if (const auto it = context.settings.find("log.rotate.max_size"); it != context.settings.end())
    {
        const auto value = ParseSize(it->second);
        if (!value.has_value())
        {
            std::cerr << "invalid log.rotate.max_size value: " << it->second << std::endl;
            return false;
        }
        context.log_max_size = value.value();
    }

    if (const auto it = context.settings.find("log.rotate.max_files"); it != context.settings.end())
    {
        const auto value = ParseUnsigned(it->second);
        if (!value.has_value() || value.value() == 0)
        {
            std::cerr << "invalid log.rotate.max_files value: " << it->second << std::endl;
            return false;
        }
        context.log_max_files = value.value();
    }

    if (const auto it = context.settings.find("log.rotate.daily_hour"); it != context.settings.end())
    {
        const auto value = ParseUnsigned(it->second);
        if (!value.has_value() || value.value() > 23)
        {
            std::cerr << "invalid log.rotate.daily_hour value: " << it->second << std::endl;
            return false;
        }
        context.log_rotate_hour = value.value();
    }

    if (const auto it = context.settings.find("log.rotate.daily_minute"); it != context.settings.end())
    {
        const auto value = ParseUnsigned(it->second);
        if (!value.has_value() || value.value() > 59)
        {
            std::cerr << "invalid log.rotate.daily_minute value: " << it->second << std::endl;
            return false;
        }
        context.log_rotate_minute = value.value();
    }

    if (const auto it = context.settings.find("runtime.daemon"); it != context.settings.end())
    {
        const auto daemon = ParseBool(it->second);
        if (!daemon.has_value())
        {
            std::cerr << "invalid runtime.daemon value: " << it->second << std::endl;
            return false;
        }
        context.daemon = daemon.value();
    }

    return true;
}
