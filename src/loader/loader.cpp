#include "loader.h"
#include "config.h"
#include "logging.h"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <iostream>
#include <optional>

namespace
{
    enum class ParseResult
    {
        ok,
        exit_success,
        exit_failure
    };

    ParseResult ParseArguments(int argc,
                               char* argv[],
                               const std::string& application_name,
                               std::optional<std::string>& override_config_path,
                               std::vector<std::string>& positional_args)
    {
        CLI::App cli{application_name + " startup loader"};
        std::string config_path_option;

        cli.set_help_flag("-h,--help", "Show this help message");
        cli.add_option("-c,--config", config_path_option, "Load a JSON configuration overlay file");
        cli.add_option("args", positional_args, "Positional arguments")->expected(0, -1);
        cli.positionals_at_end(true);

        try
        {
            cli.parse(argc, argv);
        }
        catch (const CLI::ParseError& error)
        {
            const auto exit_code = cli.exit(error);
            return exit_code == 0 ? ParseResult::exit_success : ParseResult::exit_failure;
        }

        if (!config_path_option.empty())
        {
            override_config_path = std::move(config_path_option);
        }

        return ParseResult::ok;
    }
}

int Loader::Run(IApplication& app, int argc, char* argv[])
{
    const std::string application_name = app.GetName();
    std::optional<std::string> override_config_path;
    std::vector<std::string> positional_args;
    const ParseResult parse_result =
        ParseArguments(argc, argv, application_name, override_config_path, positional_args);
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
    loader_config.arguments = std::move(positional_args);
    std::unique_ptr<IApplicationConfiguration> app_config;

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
