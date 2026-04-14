#include "loader.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <utility>

namespace
{
    struct StartupOptions
    {
        std::string config_path;
        bool config_path_explicit = false;
        bool show_help = false;
        bool show_version = false;
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

    std::string Narrow(const char8_t* value)
    {
        if (value == nullptr)
        {
            return {};
        }
        return reinterpret_cast<const char*>(value);
    }

    bool ParseArguments(int argc, char* argv[], StartupOptions& options)
    {
        for (int index = 1; index < argc; ++index)
        {
            const std::string arg = argv[index];
            if (arg == "--")
            {
                for (++index; index < argc; ++index)
                {
                    options.positional_args.emplace_back(argv[index]);
                }
                return true;
            }

            if (arg == "-h" || arg == "--help")
            {
                options.show_help = true;
                continue;
            }

            if (arg == "-V" || arg == "--version")
            {
                options.show_version = true;
                continue;
            }

            if (arg == "-v" || arg == "--verbose")
            {
                options.verbose = true;
                continue;
            }

            if (arg == "-c" || arg == "--config")
            {
                if (index + 1 >= argc)
                {
                    std::cerr << "missing value for " << arg << std::endl;
                    return false;
                }
                options.config_path = argv[++index];
                options.config_path_explicit = true;
                continue;
            }

            if (arg.rfind("--config=", 0) == 0)
            {
                options.config_path = arg.substr(std::string("--config=").size());
                options.config_path_explicit = true;
                continue;
            }

            if (!arg.empty() && arg[0] == '-')
            {
                std::cerr << "unknown option: " << arg << std::endl;
                return false;
            }

            options.positional_args.push_back(arg);
        }

        return true;
    }

    void PrintHelp(const std::string& program_name, const std::string& application_name)
    {
        std::cout << "Usage: " << program_name << " [options] [args]" << std::endl;
        std::cout << "Application: " << application_name << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  -h, --help            Show this help message" << std::endl;
        std::cout << "  -V, --version         Show application name and version banner" << std::endl;
        std::cout << "  -v, --verbose         Enable verbose startup logs" << std::endl;
        std::cout << "  -c, --config <path>   Load the specified configuration file" << std::endl;
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
    if (!ParseArguments(argc, argv, options))
    {
        PrintHelp(argc > 0 ? argv[0] : "app", Narrow(app->GetName()));
        return 1;
    }

    if (options.show_help)
    {
        PrintHelp(argc > 0 ? argv[0] : "app", Narrow(app->GetName()));
        return 0;
    }

    if (options.show_version)
    {
        std::cout << Narrow(app->GetName()) << " version 0.1.0" << std::endl;
        return 0;
    }

    ApplicationContext context;
    context.executable_path = argc > 0 ? argv[0] : "";
    context.config_path = ResolveConfigPath(options, *app);
    context.verbose = options.verbose;
    context.arguments = std::move(options.positional_args);

    if (!LoadConfiguration(context, options.config_path_explicit))
    {
        return 1;
    }

    if (context.verbose)
    {
        std::cout << "starting " << Narrow(app->GetName()) << std::endl;
        if (!context.config_path.empty())
        {
            std::cout << "using config: " << context.config_path << std::endl;
        }
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
