#include "options.h"

#include "framework/application/application.h"

#include <CLI/CLI.hpp>

#include <utility>

namespace
{
    const std::vector<std::string> kLogLevels{"trace", "debug", "info", "warn", "error", "fatal"};
    const std::vector<std::string> kRotationModes{"size", "daily"};
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
    CLI::App cli{application_name + " startup loader"};
    bool daemon = false;
    bool syslog = false;
    bool disable_console = false;
    bool disable_file_log = false;
    bool disable_error_log = false;
    std::string config_path_option;
    std::string pid_file_option;
    std::string log_file_option;
    std::string error_log_file_option;
    std::string log_level_option;
    std::string log_rotation_mode_option;
    std::size_t log_max_size_value = 0;
    std::size_t log_max_files_value = 0;
    std::size_t log_rotate_hour_value = 0;
    std::size_t log_rotate_minute_value = 0;

    cli.set_help_flag("-h,--help", "Show this help message");
    cli.add_flag("-V,--version", options.show_version, "Show application name and version banner");
    cli.add_flag("-d,--daemon", daemon, "Run in daemon mode");
    cli.add_flag("--syslog", syslog, "Enable syslog sink");
    cli.add_flag("--no-console", disable_console, "Disable console logging");
    cli.add_flag("--no-file-log", disable_file_log, "Disable the main log file sink");
    cli.add_flag("--no-error-log", disable_error_log, "Disable the dedicated error log sink");
    cli.add_flag("-v,--verbose", options.verbose, "Enable verbose startup logs");
    cli.add_option("-c,--config", config_path_option, "Load the specified JSON configuration file");
    cli.add_option("--pid-file", pid_file_option, "Write the running process id to this file");
    cli.add_option("--log-file", log_file_option, "Override log file path");
    cli.add_option("--error-log-file", error_log_file_option, "Write error logs to a separate file");
    auto* log_level = cli.add_option("--log-level", log_level_option, "Override log level")
        ->check(CLI::IsMember(kLogLevels, CLI::ignore_case));
    auto* log_rotation_mode =
        cli.add_option("--log-rotate-mode", log_rotation_mode_option, "Select log rotation mode")
        ->check(CLI::IsMember(kRotationModes, CLI::ignore_case));
    auto* log_max_size =
        cli.add_option("--log-max-size", log_max_size_value, "Override rotating log max size in bytes");
    auto* log_max_files =
        cli.add_option("--log-max-files", log_max_files_value, "Override rotating log file count");
    auto* log_rotate_hour =
        cli.add_option("--log-rotate-hour", log_rotate_hour_value, "Daily rotation hour (0-23)")
            ->check(CLI::Range(0, 23));
    auto* log_rotate_minute =
        cli.add_option("--log-rotate-minute", log_rotate_minute_value, "Daily rotation minute (0-59)")
            ->check(CLI::Range(0, 59));
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

    if (!log_file_option.empty())
    {
        options.log_file = std::move(log_file_option);
    }

    if (!error_log_file_option.empty())
    {
        options.error_log_file = std::move(error_log_file_option);
    }

    if (log_level->count() > 0)
    {
        options.log_level = std::move(log_level_option);
    }

    if (log_rotation_mode->count() > 0)
    {
        options.log_rotation_mode = std::move(log_rotation_mode_option);
    }

    if (log_max_size->count() > 0)
    {
        options.log_max_size = log_max_size_value;
    }

    if (log_max_files->count() > 0)
    {
        options.log_max_files = log_max_files_value;
    }

    if (log_rotate_hour->count() > 0)
    {
        options.log_rotate_hour = log_rotate_hour_value;
    }

    if (log_rotate_minute->count() > 0)
    {
        options.log_rotate_minute = log_rotate_minute_value;
    }

    if (daemon)
    {
        options.daemon = true;
    }

    if (syslog)
    {
        options.syslog = true;
    }

    if (disable_console)
    {
        options.console = false;
    }

    if (disable_file_log)
    {
        options.file_log_enabled_override = false;
    }

    if (disable_error_log)
    {
        options.error_log_enabled_override = false;
    }

    return ParseResult::ok;
}
