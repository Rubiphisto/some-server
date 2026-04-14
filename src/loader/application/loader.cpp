#include "loader.h"

#include <CLI/CLI.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>

namespace
{
    enum class ParseResult
    {
        ok,
        exit_success,
        exit_failure
    };

    struct StartupOptions
    {
        std::string config_path;
        std::string log_level;
        bool config_path_explicit = false;
        bool show_version = false;
        bool daemon = false;
        bool verbose = false;
        std::vector<std::string> positional_args;
    };

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

    std::string Narrow(const char8_t* value)
    {
        if (value == nullptr)
        {
            return {};
        }
        return reinterpret_cast<const char*>(value);
    }

    ParseResult ParseArguments(int argc, char* argv[], const std::string& application_name, StartupOptions& options)
    {
        CLI::App app{application_name + " startup loader"};
        std::string config_path_option;
        const std::vector<std::string> log_levels{"trace", "debug", "info", "warn", "error", "fatal"};

        app.set_help_flag("-h,--help", "Show this help message");
        app.add_flag("-V,--version", options.show_version, "Show application name and version banner");
        app.add_flag("-d,--daemon", options.daemon, "Run in daemon mode");
        app.add_flag("-v,--verbose", options.verbose, "Enable verbose startup logs");
        app.add_option("-c,--config", config_path_option, "Load the specified configuration file");
        app.add_option("--log-level", options.log_level, "Override log level")
            ->check(CLI::IsMember(log_levels, CLI::ignore_case));
        app.add_option("args", options.positional_args, "Positional arguments")->expected(0, -1);
        app.positionals_at_end(true);

        try
        {
            app.parse(argc, argv);
        }
        catch (const CLI::ParseError& error)
        {
            const auto exit_code = app.exit(error);
            return exit_code == 0 ? ParseResult::exit_success : ParseResult::exit_failure;
        }

        if (!config_path_option.empty())
        {
            options.config_path = std::move(config_path_option);
            options.config_path_explicit = true;
        }

        return ParseResult::ok;
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

    std::string ResolveConfigPath(const StartupOptions& options, const IApplication& application)
    {
        if (options.config_path_explicit)
        {
            return options.config_path;
        }

        return (std::filesystem::path("conf") / (Narrow(application.GetName()) + ".conf")).string();
    }
}

Loader::Loader(IApplicationFactory& factory)
    : mFactory(factory)
{
}

int Loader::Run(int argc, char* argv[])
{
    auto app = mFactory.Create();
    if (!app)
    {
        std::cerr << "failed to create application" << std::endl;
        return 1;
    }

    StartupOptions options;
    const std::string application_name = Narrow(app->GetName());
    const ParseResult parse_result = ParseArguments(argc, argv, application_name, options);
    if (parse_result == ParseResult::exit_success)
    {
        return 0;
    }

    if (parse_result == ParseResult::exit_failure)
    {
        return 1;
    }

    if (options.show_version)
    {
        std::cout << application_name << " version 0.1.0" << std::endl;
        return 0;
    }

    ApplicationContext context;
    context.executable_path = argc > 0 ? argv[0] : "";
    context.config_path = ResolveConfigPath(options, *app);
    context.arguments = std::move(options.positional_args);

    if (!LoadConfiguration(context, options.config_path_explicit))
    {
        return 1;
    }

    if (!options.log_level.empty())
    {
        context.log_level = options.log_level;
        context.settings["log.level"] = options.log_level;
    }

    if (options.daemon)
    {
        context.daemon = true;
        context.settings["runtime.daemon"] = "true";
    }

    context.verbose = options.verbose;

    if (context.verbose)
    {
        std::cout << "starting " << application_name << std::endl;
        if (!context.config_path.empty())
        {
            std::cout << "using config: " << context.config_path << std::endl;
        }
        std::cout << "log level: " << context.log_level << std::endl;
        std::cout << "daemon mode: " << (context.daemon ? "enabled" : "disabled") << std::endl;
    }

    if (!Initialize(*app, context))
    {
        return 1;
    }

    return 0;
}

bool Loader::Initialize(IApplication& app, const ApplicationContext& context) const
{
    if (!app.Configure(context))
    {
        std::cerr << "application configure failed" << std::endl;
        return false;
    }

    app.Load();
    app.Start();
    app.Stop();
    app.Unload();
    return true;
}
