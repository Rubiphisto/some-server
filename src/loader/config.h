#pragma once

#include "framework/application/application.h"

#include <cstddef>
#include <string>
#include <unordered_map>

class IApplication;
struct StartupOptions;

struct ListenConfig
{
    std::string host = "127.0.0.1";
    std::size_t port = 9000;
};

struct LogConfig
{
    std::string file;
    std::string error_file;
    std::string level = "info";
    std::string rotation_mode = "size";
    std::size_t max_size = 10 * 1024 * 1024;
    std::size_t max_files = 5;
    std::size_t rotate_hour = 0;
    std::size_t rotate_minute = 0;
    bool console = true;
    bool syslog = false;
};

struct RuntimeConfig
{
    std::string pid_file;
    bool daemon = false;
};

struct LoaderConfig
{
    std::string config_path;
    ListenConfig listen;
    LogConfig log;
    RuntimeConfig runtime;
    std::unordered_map<std::string, std::string> settings;
};

LoaderConfig BuildDefaultConfig(const StartupOptions& options, const IApplication& application);
bool LoadYamlConfig(LoaderConfig& config, bool config_path_explicit, bool verbose);
void ApplyCliOverrides(LoaderConfig& config, const StartupOptions& options);
bool ValidateConfig(const LoaderConfig& config);
void CopyToContext(const LoaderConfig& config, ApplicationContext& context);
bool ResolveConfiguration(ApplicationContext& context, const StartupOptions& options, const IApplication& application);
