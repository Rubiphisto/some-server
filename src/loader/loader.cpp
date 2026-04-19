#include "loader.h"
#include "config.h"
#include "logging.h"

#include <spdlog/spdlog.h>

#include <array>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    enum class ParseResult
    {
        ok,
        exit_success,
        exit_failure
    };

    struct OptionDefinition
    {
        std::string_view long_name;
        std::string_view short_name;
        std::string_view value_format;
        std::string_view description;
        bool expects_value = false;
    };

    constexpr std::array<OptionDefinition, 2> kOptionDefinitions{{
        {"help", "h", "", "Show this help message", false},
        {"config", "c", "<path>", "Load a JSON configuration overlay file", true},
    }};

    void PrintHelp(const std::string& application_name)
    {
        std::cout << application_name << " startup loader\n\n"
                  << "./" << application_name << " [OPTIONS]\n\n"
                  << "OPTIONS:\n";

        for (const auto& option : kOptionDefinitions)
        {
            std::string option_usage =
                "  -" + std::string{option.short_name} + ", --" + std::string{option.long_name};
            if (option.expects_value && !option.value_format.empty())
            {
                option_usage += ' ';
                option_usage += option.value_format;
            }

            std::cout << std::left << std::setw(30) << option_usage << option.description << '\n';
        }
    }

    ParseResult ParseArguments(int argc,
                               char* argv[],
                               const std::string& application_name,
                               std::map<std::string, std::string>& option_values)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view argument = argv[i];

            const OptionDefinition* matched_option = nullptr;
            if (argument.starts_with("--"))
            {
                const std::string_view long_name = argument.substr(2);
                for (const auto& option : kOptionDefinitions)
                {
                    if (option.long_name == long_name)
                    {
                        matched_option = &option;
                        break;
                    }
                }
            }
            else if (argument.starts_with('-'))
            {
                const std::string_view short_name = argument.substr(1);
                for (const auto& option : kOptionDefinitions)
                {
                    if (option.short_name == short_name)
                    {
                        matched_option = &option;
                        break;
                    }
                }
            }

            if (matched_option != nullptr)
            {
                if (!matched_option->expects_value)
                {
                    option_values.emplace(std::string{matched_option->long_name}, std::string{});
                    continue;
                }

                if (i + 1 >= argc)
                {
                    std::cerr << "missing value for " << argument << std::endl;
                    return ParseResult::exit_failure;
                }

                option_values[std::string{matched_option->long_name}] = argv[++i];
                continue;
            }

            if (argument.starts_with('-'))
            {
                std::cerr << "unknown option: " << argument << std::endl;
                return ParseResult::exit_failure;
            }
            std::cerr << "unexpected positional argument: " << argument << std::endl;
            return ParseResult::exit_failure;
        }

        if (option_values.contains("help"))
        {
            PrintHelp(application_name);
            return ParseResult::exit_success;
        }

        return ParseResult::ok;
    }
}

int Loader::Run(IApplication& app, int argc, char* argv[])
{
    const std::string application_name = app.GetName();
    std::map<std::string, std::string> option_values;
    const ParseResult parse_result = ParseArguments(argc, argv, application_name, option_values);
    if (parse_result == ParseResult::exit_success)
    {
        return 0;
    }

    if (parse_result == ParseResult::exit_failure)
    {
        return 1;
    }

    LoaderConfiguration loader_config;
    loader_config.executable_path = argc > 0 ? argv[0] : "";
    std::unique_ptr<IApplicationConfiguration> app_config;
    const auto config_it = option_values.find("config");
    const std::optional<std::string> override_config_path =
        config_it != option_values.end() ? std::optional<std::string>{config_it->second} : std::nullopt;

    if (!ResolveConfiguration(loader_config, app_config, override_config_path, app))
    {
        return 1;
    }

    if (!SetupLogging(application_name, loader_config))
    {
        return 1;
    }

    if (loader_config.verbose)
    {
        spdlog::info("starting {}", application_name);
        if (!loader_config.config_path.empty())
        {
            spdlog::info("using main config: {}", loader_config.config_path);
        }
        if (!loader_config.override_config_path.empty())
        {
            spdlog::info("using override config: {}", loader_config.override_config_path);
        }
        if (!loader_config.log.file.empty())
        {
            spdlog::info("writing logs to: {}", loader_config.log.file);
        }
        if (!loader_config.log.error_file.empty())
        {
            spdlog::info("writing error logs to: {}", loader_config.log.error_file);
        }
        spdlog::info("log level: {}", loader_config.log.level);
        spdlog::info("log rotation mode: {}", loader_config.log.rotate.mode);
        if (loader_config.log.rotate.mode == "daily")
        {
            spdlog::info("daily rotation time: {:02d}:{:02d} max_files={}",
                         static_cast<int>(loader_config.log.rotate.daily_hour),
                         static_cast<int>(loader_config.log.rotate.daily_minute),
                         loader_config.log.rotate.max_files);
        }
        else
        {
            spdlog::info("log rotation: max_size={} max_files={}",
                         loader_config.log.rotate.max_size,
                         loader_config.log.rotate.max_files);
        }
        spdlog::info("console logging: {}", loader_config.log.console ? "enabled" : "disabled");
        spdlog::info("syslog logging: {}", loader_config.log.syslog ? "enabled" : "disabled");
    }

    if (!Initialize(app, *app_config))
    {
        spdlog::shutdown();
        return 1;
    }

    spdlog::shutdown();
    return 0;
}

bool Loader::Initialize(IApplication& app, const IApplicationConfiguration& configuration) const
{
    if (!app.Configure(configuration))
    {
        spdlog::error("application configure failed");
        return false;
    }

    app.Load();
    app.Start();
    app.Stop();
    app.Unload();
    return true;
}
