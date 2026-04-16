#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct ApplicationContext
{
    std::string executable_path;
    std::string config_path;
    std::string pid_file;
    std::string log_file;
    std::string error_log_file;
    std::string log_level = "info";
    std::string log_rotation_mode = "size";
    std::size_t log_max_size = 10 * 1024 * 1024;
    std::size_t log_max_files = 5;
    std::size_t log_rotate_hour = 0;
    std::size_t log_rotate_minute = 0;
    bool daemon = false;
    bool log_to_console = true;
    bool log_to_syslog = false;
    bool verbose = false;
    std::vector<std::string> arguments;
    std::unordered_map<std::string, std::string> settings;

    std::string GetSetting(const std::string& key, std::string default_value = {}) const
    {
        const auto it = settings.find(key);
        return it == settings.end() ? std::move(default_value) : it->second;
    }
};

class IApplication
{
public:
    virtual ~IApplication() = default;
    virtual const char8_t* GetName() const = 0;
    virtual bool Configure(const ApplicationContext& context) = 0;
    virtual void Load() = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Unload() = 0;
};
