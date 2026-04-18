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
    std::optional<std::string> config_path;
    std::optional<std::string> pid_file;
    std::optional<bool> daemon;
    std::vector<std::string> positional_args;
};

ParseResult ParseArguments(int argc, char* argv[], const std::string& application_name, StartupOptions& options);
