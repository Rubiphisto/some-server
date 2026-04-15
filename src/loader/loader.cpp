#include "loader.h"
#include "loader_config.h"
#include "loader_logging.h"
#include "loader_options.h"

#include <spdlog/spdlog.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <utility>

namespace
{
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

Loader::Loader(ApplicationFactory factory, ApplicationDestroyer destroyer)
    : mFactory(std::move(factory))
    , mDestroyer(std::move(destroyer))
{
}

int Loader::Run(int argc, char* argv[])
{
    ApplicationDestroyer destroyer = mDestroyer;
    if (!destroyer)
    {
        destroyer = [](IApplication* app) { delete app; };
    }

    auto app = std::unique_ptr<IApplication, ApplicationDestroyer>(
        mFactory ? mFactory() : nullptr,
        std::move(destroyer));
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
        context.log_rotation_mode = options.log_rotation_mode;
        context.settings["log.rotate.mode"] = options.log_rotation_mode;
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
