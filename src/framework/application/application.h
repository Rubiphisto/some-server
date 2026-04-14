#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct ApplicationContext
{
    std::string executable_path;
    std::string config_path;
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

class IApplicationFactory
{
public:
    virtual ~IApplicationFactory() = default;
    virtual std::unique_ptr<IApplication> Create() = 0;
};
