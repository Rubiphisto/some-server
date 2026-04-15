#pragma once

#include <cstddef>
#include <string>
#include <vector>

class IApplication;

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

std::string Narrow(const char8_t* value);
ParseResult ParseArguments(int argc, char* argv[], const std::string& application_name, StartupOptions& options);
std::string ResolveConfigPath(const StartupOptions& options, const IApplication& application);
std::string ResolvePidFilePath(const StartupOptions& options, const IApplication& application);
std::string ResolveLogFilePath(const StartupOptions& options, const IApplication& application);
std::string ResolveErrorLogFilePath(const StartupOptions& options, const IApplication& application);
