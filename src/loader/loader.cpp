#include "loader.h"

#include <CLI/CLI.hpp>
#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#ifndef _WIN32
#include <fcntl.h>
#include <spdlog/sinks/syslog_sink.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <cctype>
#include <cstdio>
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
        std::string pid_file;
        std::string log_file;
        std::string error_log_file;
        std::string log_level;
        std::string log_rotation_mode;
        std::size_t log_max_size = 0;
        std::size_t log_max_files = 0;
        std::size_t log_rotate_hour = 0;
        std::size_t log_rotate_minute = 0;
        bool config_path_explicit = false;
        bool show_version = false;
        bool daemon = false;
        bool syslog = false;
        bool disable_console = false;
        bool disable_file_log = false;
        bool disable_error_log = false;
        bool verbose = false;
        std::vector<std::string> positional_args;
    };

    struct PidFileGuard
    {
        std::string path;

        ~PidFileGuard()
        {
            if (!path.empty())
            {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        }
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
        const std::vector<std::string> log_levels{"trace", "debug", "info", "warn", "error", "fatal"};
        const std::vector<std::string> rotation_modes{"size", "daily"};

        app.set_help_flag("-h,--help", "Show this help message");
        app.add_flag("-V,--version", options.show_version, "Show application name and version banner");
        app.add_flag("-d,--daemon", options.daemon, "Run in daemon mode");
        app.add_flag("--syslog", options.syslog, "Enable syslog sink");
        app.add_flag("--no-console", options.disable_console, "Disable console logging");
        app.add_flag("--no-file-log", options.disable_file_log, "Disable the main log file sink");
        app.add_flag("--no-error-log", options.disable_error_log, "Disable the dedicated error log sink");
        app.add_flag("-v,--verbose", options.verbose, "Enable verbose startup logs");
        app.add_option("-c,--config", config_path_option, "Load the specified configuration file");
        app.add_option("--pid-file", pid_file_option, "Write the running process id to this file");
        app.add_option("--log-file", log_file_option, "Override log file path");
        app.add_option("--error-log-file", error_log_file_option, "Write error logs to a separate file");
        app.add_option("--log-level", options.log_level, "Override log level")
            ->check(CLI::IsMember(log_levels, CLI::ignore_case));
        app.add_option("--log-rotate-mode", options.log_rotation_mode, "Select log rotation mode")
            ->check(CLI::IsMember(rotation_modes, CLI::ignore_case));
        app.add_option("--log-max-size", options.log_max_size, "Override rotating log max size in bytes");
        app.add_option("--log-max-files", options.log_max_files, "Override rotating log file count");
        app.add_option("--log-rotate-hour", options.log_rotate_hour, "Daily rotation hour (0-23)")
            ->check(CLI::Range(0, 23));
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
            options.config_path_explicit = true;
        }

        if (!log_file_option.empty())
        {
            options.log_file = std::move(log_file_option);
        }

        if (!pid_file_option.empty())
        {
            options.pid_file = std::move(pid_file_option);
        }

        if (!error_log_file_option.empty())
        {
            options.error_log_file = std::move(error_log_file_option);
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

    std::string ResolveConfigPath(const StartupOptions& options, const IApplication& application)
    {
        if (options.config_path_explicit)
        {
            return options.config_path;
        }

        return (std::filesystem::path("conf") / (Narrow(application.GetName()) + ".conf")).string();
    }

    std::string ResolveLogFilePath(const StartupOptions& options, const IApplication& application)
    {
        if (!options.log_file.empty())
        {
            return options.log_file;
        }

        return (std::filesystem::path("logs") / (Narrow(application.GetName()) + ".log")).string();
    }

    std::string ResolvePidFilePath(const StartupOptions& options, const IApplication& application)
    {
        if (!options.pid_file.empty())
        {
            return options.pid_file;
        }

        return (std::filesystem::path("run") / (Narrow(application.GetName()) + ".pid")).string();
    }

    std::string ResolveErrorLogFilePath(const StartupOptions& options, const IApplication& application)
    {
        if (!options.error_log_file.empty())
        {
            return options.error_log_file;
        }

        return (std::filesystem::path("logs") / (Narrow(application.GetName()) + ".error.log")).string();
    }

    std::optional<spdlog::level::level_enum> ToSpdlogLevel(std::string level)
    {
        level = ToLower(std::move(level));

        if (level == "trace")
        {
            return spdlog::level::trace;
        }
        if (level == "debug")
        {
            return spdlog::level::debug;
        }
        if (level == "info")
        {
            return spdlog::level::info;
        }
        if (level == "warn" || level == "warning")
        {
            return spdlog::level::warn;
        }
        if (level == "error")
        {
            return spdlog::level::err;
        }
        if (level == "fatal" || level == "critical")
        {
            return spdlog::level::critical;
        }

        return std::nullopt;
    }

    bool SetupLogging(const std::string& application_name, ApplicationContext& context)
    {
        const auto level = ToSpdlogLevel(context.log_level);
        if (!level.has_value())
        {
            std::cerr << "invalid log level: " << context.log_level << std::endl;
            return false;
        }

        try
        {
            std::vector<spdlog::sink_ptr> sinks;

            if (context.log_to_console)
            {
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
                sinks.push_back(console_sink);
            }

            if (!context.log_file.empty())
            {
                const auto log_path = std::filesystem::path(context.log_file);
                if (log_path.has_parent_path())
                {
                    std::filesystem::create_directories(log_path.parent_path());
                }

                spdlog::sink_ptr file_sink;
                if (context.log_rotation_mode == "daily")
                {
                    file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
                        context.log_file,
                        static_cast<int>(context.log_rotate_hour),
                        static_cast<int>(context.log_rotate_minute),
                        false,
                        static_cast<uint16_t>(context.log_max_files));
                }
                else
                {
                    file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                        context.log_file,
                        context.log_max_size,
                        context.log_max_files,
                        true);
                }
                file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");
                sinks.push_back(file_sink);
            }

            if (!context.error_log_file.empty())
            {
                const auto error_log_path = std::filesystem::path(context.error_log_file);
                if (error_log_path.has_parent_path())
                {
                    std::filesystem::create_directories(error_log_path.parent_path());
                }

                auto error_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(context.error_log_file, true);
                error_sink->set_level(spdlog::level::err);
                error_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");
                sinks.push_back(error_sink);
            }

#ifndef _WIN32
            if (context.log_to_syslog)
            {
                auto syslog_sink = std::make_shared<spdlog::sinks::syslog_sink_mt>(
                    application_name,
                    0,
                    LOG_USER,
                    true);
                syslog_sink->set_pattern("[%l] [%n] %v");
                sinks.push_back(syslog_sink);
            }
#endif

            if (sinks.empty())
            {
                std::cerr << "no log sinks enabled" << std::endl;
                return false;
            }

            auto logger = std::make_shared<spdlog::logger>(application_name, sinks.begin(), sinks.end());
            logger->set_level(level.value());
            logger->flush_on(spdlog::level::err);
            spdlog::set_default_logger(logger);
            spdlog::set_level(level.value());
            return true;
        }
        catch (const spdlog::spdlog_ex& ex)
        {
            std::cerr << "failed to initialize logging: " << ex.what() << std::endl;
            return false;
        }
    }

#ifndef _WIN32
    bool CreatePidFile(const std::string& pid_file)
    {
        if (pid_file.empty())
        {
            return true;
        }

        const auto pid_path = std::filesystem::path(pid_file);
        if (pid_path.has_parent_path())
        {
            std::filesystem::create_directories(pid_path.parent_path());
        }

        int fd = ::open(pid_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1)
        {
            std::perror("open pid file");
            return false;
        }

        const std::string pid_text = std::to_string(::getpid());
        const auto written = ::write(fd, pid_text.c_str(), pid_text.size());
        ::close(fd);
        if (written < 0 || static_cast<std::size_t>(written) != pid_text.size())
        {
            std::perror("write pid file");
            return false;
        }

        return true;
    }

    bool DaemonizeProcess()
    {
        const pid_t first_fork = ::fork();
        if (first_fork < 0)
        {
            std::perror("fork");
            return false;
        }

        if (first_fork > 0)
        {
            std::exit(0);
        }

        if (::setsid() < 0)
        {
            std::perror("setsid");
            return false;
        }

        const pid_t second_fork = ::fork();
        if (second_fork < 0)
        {
            std::perror("fork");
            return false;
        }

        if (second_fork > 0)
        {
            std::exit(0);
        }

        const int null_fd = ::open("/dev/null", O_RDWR);
        if (null_fd < 0)
        {
            std::perror("open /dev/null");
            return false;
        }

        ::dup2(null_fd, STDIN_FILENO);
        ::dup2(null_fd, STDOUT_FILENO);
        ::dup2(null_fd, STDERR_FILENO);
        if (null_fd > STDERR_FILENO)
        {
            ::close(null_fd);
        }

        return true;
    }
#endif
}

Loader::Loader(ApplicationFactory factory)
    : mFactory(std::move(factory))
{
}

int Loader::Run(int argc, char* argv[])
{
    auto app = std::unique_ptr<IApplication>(mFactory ? mFactory() : nullptr);
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
    context.pid_file = ResolvePidFilePath(options, *app);
    context.log_file = ResolveLogFilePath(options, *app);
    context.error_log_file = ResolveErrorLogFilePath(options, *app);
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

    if (!options.pid_file.empty())
    {
        context.pid_file = options.pid_file;
        context.settings["runtime.pid_file"] = options.pid_file;
    }

    if (!options.log_file.empty())
    {
        context.log_file = options.log_file;
        context.settings["log.file"] = options.log_file;
    }

    if (!options.error_log_file.empty())
    {
        context.error_log_file = options.error_log_file;
        context.settings["log.error_file"] = options.error_log_file;
    }

    if (options.daemon)
    {
        context.daemon = true;
        context.settings["runtime.daemon"] = "true";
        context.log_to_console = false;
        context.settings["log.console"] = "false";
    }

    if (options.syslog)
    {
        context.log_to_syslog = true;
        context.settings["log.syslog"] = "true";
    }

    if (options.disable_console)
    {
        context.log_to_console = false;
        context.settings["log.console"] = "false";
    }

    if (options.disable_file_log)
    {
        context.log_file.clear();
        context.settings["log.file"] = "";
    }

    if (options.disable_error_log)
    {
        context.error_log_file.clear();
        context.settings["log.error_file"] = "";
    }

    if (options.log_max_size != 0)
    {
        context.log_max_size = options.log_max_size;
        context.settings["log.rotate.max_size"] = std::to_string(options.log_max_size);
    }

    if (options.log_max_files != 0)
    {
        context.log_max_files = options.log_max_files;
        context.settings["log.rotate.max_files"] = std::to_string(options.log_max_files);
    }

    if (!options.log_rotation_mode.empty())
    {
        context.log_rotation_mode = ToLower(options.log_rotation_mode);
        context.settings["log.rotate.mode"] = context.log_rotation_mode;
    }

    if (options.log_rotate_hour != 0 || context.settings.contains("log.rotate.daily_hour"))
    {
        context.log_rotate_hour = options.log_rotate_hour;
        context.settings["log.rotate.daily_hour"] = std::to_string(options.log_rotate_hour);
    }

    if (options.log_rotate_minute != 0 || context.settings.contains("log.rotate.daily_minute"))
    {
        context.log_rotate_minute = options.log_rotate_minute;
        context.settings["log.rotate.daily_minute"] = std::to_string(options.log_rotate_minute);
    }

    context.verbose = options.verbose;

#ifndef _WIN32
    if (context.daemon && !DaemonizeProcess())
    {
        return 1;
    }
#endif

    PidFileGuard pid_file_guard;
    if (!context.pid_file.empty())
    {
#ifndef _WIN32
        if (!CreatePidFile(context.pid_file))
        {
            return 1;
        }
#endif
        pid_file_guard.path = context.pid_file;
    }

    if (!SetupLogging(application_name, context))
    {
        return 1;
    }

    if (context.verbose)
    {
        spdlog::info("starting {}", application_name);
        if (!context.config_path.empty())
        {
            spdlog::info("using config: {}", context.config_path);
        }
        if (!context.pid_file.empty())
        {
            spdlog::info("writing pid file to: {}", context.pid_file);
        }
        if (!context.log_file.empty())
        {
            spdlog::info("writing logs to: {}", context.log_file);
        }
        if (!context.error_log_file.empty())
        {
            spdlog::info("writing error logs to: {}", context.error_log_file);
        }
        spdlog::info("log level: {}", context.log_level);
        spdlog::info("log rotation mode: {}", context.log_rotation_mode);
        if (context.log_rotation_mode == "daily")
        {
            spdlog::info("daily rotation time: {:02d}:{:02d} max_files={}",
                         static_cast<int>(context.log_rotate_hour),
                         static_cast<int>(context.log_rotate_minute),
                         context.log_max_files);
        }
        else
        {
            spdlog::info("log rotation: max_size={} max_files={}", context.log_max_size, context.log_max_files);
        }
        spdlog::info("console logging: {}", context.log_to_console ? "enabled" : "disabled");
        spdlog::info("syslog logging: {}", context.log_to_syslog ? "enabled" : "disabled");
        spdlog::info("daemon mode: {}", context.daemon ? "enabled" : "disabled");
    }

    if (!Initialize(*app, context))
    {
        spdlog::shutdown();
        return 1;
    }

    spdlog::shutdown();
    return 0;
}

bool Loader::Initialize(IApplication& app, const ApplicationContext& context) const
{
    if (!app.Configure(context))
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
