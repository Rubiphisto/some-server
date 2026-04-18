#pragma once

#include <cstddef>
#include <optional>
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
    bool show_version = false;
    bool verbose = false;
    std::optional<std::string> config_path;
    std::optional<std::string> pid_file;
    std::optional<std::string> log_file;
    std::optional<std::string> error_log_file;
    std::optional<std::string> log_level;
    std::optional<std::string> log_rotation_mode;
    std::optional<std::size_t> log_max_size;
    std::optional<std::size_t> log_max_files;
    std::optional<std::size_t> log_rotate_hour;
    std::optional<std::size_t> log_rotate_minute;
    std::optional<bool> daemon;
    std::optional<bool> syslog;
    std::optional<bool> console;
    std::optional<bool> file_log;
    std::optional<bool> error_log;
    std::vector<std::string> positional_args;
};

std::string Narrow(const char8_t* value);
ParseResult ParseArguments(int argc, char* argv[], const std::string& application_name, StartupOptions& options);
