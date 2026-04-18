#include "options.h"

#include "framework/application/application.h"

#include <CLI/CLI.hpp>

#include <utility>

ParseResult ParseArguments(int argc, char* argv[], const std::string& application_name, StartupOptions& options)
{
    CLI::App cli{application_name + " startup loader"};
    bool daemon = false;
    std::string config_path_option;
    std::string pid_file_option;

    cli.set_help_flag("-h,--help", "Show this help message");
    cli.add_flag("-d,--daemon", daemon, "Run in daemon mode");
    cli.add_option("-c,--config", config_path_option, "Load a JSON configuration overlay file");
    cli.add_option("--pid-file", pid_file_option, "Write the running process id to this file");
    cli.add_option("args", options.positional_args, "Positional arguments")->expected(0, -1);
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
        options.config_path = std::move(config_path_option);
    }

    if (!pid_file_option.empty())
    {
        options.pid_file = std::move(pid_file_option);
    }

    if (daemon)
    {
        options.daemon = true;
    }

    return ParseResult::ok;
}
