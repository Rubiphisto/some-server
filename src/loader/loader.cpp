#include "loader.h"
#include "config.h"
#include "logging.h"

#include <spdlog/spdlog.h>

#include <array>
#include <iomanip>
#include <iostream>
#include <map>
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
    if (ParseResult::ok != parse_result)
        return ParseResult::exit_success == parse_result ? 0 : 1;

    LoaderConfiguration loader_config = BuildDefaultLoaderConfiguration(app);
    std::unique_ptr<IApplicationConfiguration> application_configuration;
    const auto config_option = option_values.find("config");
    const std::string override_path =
        config_option != option_values.end()
            ? config_option->second
            : "conf/" + application_name + "_my.json";
    const std::string main_path = "conf/" + application_name + ".json";

    if (!LoadConfiguration(loader_config, main_path, override_path, application_configuration, app))
    {
        return 1;
    }

    if (!SetupLogging(application_name, loader_config))
    {
        return 1;
    }

    if (!Initialize(app, *application_configuration))
    {
        return 1;
    }

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
