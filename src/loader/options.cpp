#include "options.h"

#include "framework/application/application.h"

#include <CLI/CLI.hpp>

#include <filesystem>
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
    CLI::App app{application_name + " startup loader"};
    std::string config_path_option;
    std::string pid_file_option;
    std::string log_file_option;
    std::string error_log_file_option;

    app.set_help_flag("-h,--help", "Show this help message");
    app.add_flag("-V,--version", options.show_version, "Show application name and version banner");
    app.add_flag("-d,--daemon", options.daemon, "Run in daemon mode");
    app.add_flag("--syslog", options.syslog, "Enable syslog sink");
    app.add_flag("--no-console", options.disable_console, "Disable console logging");
    app.add_flag("--no-file-log", options.disable_file_log, "Disable the main log file sink");
    app.add_flag("--no-error-log", options.disable_error_log, "Disable the dedicated error log sink");
    app.add_flag("-v,--verbose", options.verbose, "Enable verbose startup logs");
    app.add_option("-c,--config", config_path_option, "Load the specified YAML configuration file");
    app.add_option("--pid-file", pid_file_option, "Write the running process id to this file");
    app.add_option("--log-file", log_file_option, "Override log file path");
    app.add_option("--error-log-file", error_log_file_option, "Write error logs to a separate file");
    app.add_option("--log-level", options.log_level, "Override log level")
        ->check(CLI::IsMember(kLogLevels, CLI::ignore_case));
    app.add_option("--log-rotate-mode", options.log_rotation_mode, "Select log rotation mode")
        ->check(CLI::IsMember(kRotationModes, CLI::ignore_case));
    auto* log_max_size_option =
        app.add_option("--log-max-size", options.log_max_size, "Override rotating log max size in bytes");
    auto* log_max_files_option =
        app.add_option("--log-max-files", options.log_max_files, "Override rotating log file count");
    auto* log_rotate_hour_option =
        app.add_option("--log-rotate-hour", options.log_rotate_hour, "Daily rotation hour (0-23)")
            ->check(CLI::Range(0, 23));
    auto* log_rotate_minute_option =
        app.add_option("--log-rotate-minute", options.log_rotate_minute, "Daily rotation minute (0-59)")
            ->check(CLI::Range(0, 59));
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

    options.log_max_size_explicit = log_max_size_option->count() > 0;
    options.log_max_files_explicit = log_max_files_option->count() > 0;
    options.log_rotate_hour_explicit = log_rotate_hour_option->count() > 0;
    options.log_rotate_minute_explicit = log_rotate_minute_option->count() > 0;

    return ParseResult::ok;
}

std::string ResolveConfigPath(const StartupOptions& options, const IApplication& application)
{
    if (!options.config_path.empty())
    {
        return options.config_path;
    }

    return (std::filesystem::path("conf") / (Narrow(application.GetName()) + ".yaml")).string();
}

std::string ResolvePidFilePath(const StartupOptions& options, const IApplication& application)
{
    if (!options.pid_file.empty())
    {
        return options.pid_file;
    }

    return (std::filesystem::path("run") / (Narrow(application.GetName()) + ".pid")).string();
}

std::string ResolveLogFilePath(const StartupOptions& options, const IApplication& application)
{
    if (!options.log_file.empty())
    {
        return options.log_file;
    }

    return (std::filesystem::path("logs") / (Narrow(application.GetName()) + ".log")).string();
}

std::string ResolveErrorLogFilePath(const StartupOptions& options, const IApplication& application)
{
    if (!options.error_log_file.empty())
    {
        return options.error_log_file;
    }

    return (std::filesystem::path("logs") / (Narrow(application.GetName()) + ".error.log")).string();
}
