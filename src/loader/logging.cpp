#include "logging.h"

#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/syslog_sink.h>
#include <spdlog/spdlog.h>

#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>

namespace
{
    std::string ToLower(std::string value)
    {
        for (char& ch : value)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
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
}

bool SetupLogging(const std::string& application_name, LoaderConfiguration& context)
{
    const auto level = ToSpdlogLevel(context.log.level);
    if (!level.has_value())
    {
        std::cerr << "invalid log level: " << context.log.level << std::endl;
        return false;
    }

    try
    {
        std::vector<spdlog::sink_ptr> sinks;

        if (context.log.console)
        {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
            sinks.push_back(console_sink);
        }

        if (!context.log.file.empty())
        {
            const auto log_path = std::filesystem::path(context.log.file);
            if (log_path.has_parent_path())
            {
                std::filesystem::create_directories(log_path.parent_path());
            }

            spdlog::sink_ptr file_sink;
            if (context.log.rotate.mode == "daily")
            {
                file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
                    context.log.file,
                    static_cast<int>(context.log.rotate.daily_hour),
                    static_cast<int>(context.log.rotate.daily_minute),
                    false,
                    static_cast<uint16_t>(context.log.rotate.max_files));
            }
            else
            {
                file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    context.log.file,
                    context.log.rotate.max_size,
                    context.log.rotate.max_files,
                    true);
            }
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");
            sinks.push_back(file_sink);
        }

        if (!context.log.error_file.empty())
        {
            const auto error_log_path = std::filesystem::path(context.log.error_file);
            if (error_log_path.has_parent_path())
            {
                std::filesystem::create_directories(error_log_path.parent_path());
            }

            auto error_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(context.log.error_file, true);
            error_sink->set_level(spdlog::level::err);
            error_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");
            sinks.push_back(error_sink);
        }

        if (context.log.syslog)
        {
            auto syslog_sink = std::make_shared<spdlog::sinks::syslog_sink_mt>(
                application_name,
                0,
                LOG_USER,
                true);
            syslog_sink->set_pattern("[%l] [%n] %v");
            sinks.push_back(syslog_sink);
        }

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
