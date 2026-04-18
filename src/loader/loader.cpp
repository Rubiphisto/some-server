#include "loader.h"
#include "config.h"
#include "logging.h"
#include "options.h"

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

int Loader::Run(IApplication& app, int argc, char* argv[])
{
    StartupOptions options;
    const std::string application_name = Narrow(app.GetName());
    const ParseResult parse_result = ParseArguments(argc, argv, application_name, options);
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
    loader_config.arguments = std::move(options.positional_args);
    std::unique_ptr<IApplicationConfiguration> app_config;

    if (!ResolveConfiguration(loader_config, app_config, options, app))
    {
        return 1;
    }

#ifndef _WIN32
    if (loader_config.runtime.daemon && !DaemonizeProcess())
    {
        return 1;
    }
#endif

    PidFileGuard pid_file_guard;
    if (!loader_config.runtime.pid_file.empty())
    {
#ifndef _WIN32
        if (!CreatePidFile(loader_config.runtime.pid_file))
        {
            return 1;
        }
#endif
        pid_file_guard.path = loader_config.runtime.pid_file;
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
        if (!loader_config.runtime.pid_file.empty())
        {
            spdlog::info("writing pid file to: {}", loader_config.runtime.pid_file);
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
        spdlog::info("daemon mode: {}", loader_config.runtime.daemon ? "enabled" : "disabled");
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
