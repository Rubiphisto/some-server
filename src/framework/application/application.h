#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct ListenSettings
{
    std::string host = "127.0.0.1";
    std::size_t port = 9000;
};

struct LogRotateSettings
{
    std::string mode = "size";
    std::size_t max_size = 10 * 1024 * 1024;
    std::size_t max_files = 5;
    std::size_t daily_hour = 0;
    std::size_t daily_minute = 0;
};

struct LogSettings
{
    std::string file;
    std::string error_file;
    std::string level = "info";
    bool console = true;
    bool syslog = false;
    LogRotateSettings rotate;
};

struct RuntimeSettings
{
    std::string pid_file;
    bool daemon = false;
};

struct ApplicationContext
{
    std::string executable_path;
    std::string config_path;
    ListenSettings listen;
    RuntimeSettings runtime;
    LogSettings log;
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

class ApplicationBase : public IApplication
{
public:
    bool Configure(const ApplicationContext& context) override
    {
        mContext = context;
        return OnConfigure();
    }

protected:
    virtual bool OnConfigure() { return true; }

    const ApplicationContext& Context() const { return mContext; }
    ApplicationContext& Context() { return mContext; }

private:
    ApplicationContext mContext;
};
