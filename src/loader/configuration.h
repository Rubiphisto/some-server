#pragma once

#include "framework/config/value.h"

#include <cstddef>
#include <string>
#include <vector>

struct LoaderRuntimeConfiguration
{
    std::string pid_file;
    bool daemon = false;

    bool OverlayFromConfig(const ConfigValue& root, std::string& error);
};

struct LoaderLogRotationConfiguration
{
    std::string mode = "size";
    std::size_t max_size = 10 * 1024 * 1024;
    std::size_t max_files = 5;
    std::size_t daily_hour = 0;
    std::size_t daily_minute = 0;

    bool OverlayFromConfig(const ConfigValue& root, std::string& error);
};

struct LoaderLogConfiguration
{
    std::string file;
    std::string error_file;
    std::string level = "info";
    bool console = true;
    bool syslog = false;
    LoaderLogRotationConfiguration rotate;

    bool OverlayFromConfig(const ConfigValue& root, std::string& error);
};

struct LoaderConfiguration
{
    std::string executable_path;
    std::string config_path;
    LoaderRuntimeConfiguration runtime;
    LoaderLogConfiguration log;
    bool verbose = false;
    std::vector<std::string> arguments;

    bool OverlayFromConfig(const ConfigValue& root, std::string& error);
};
