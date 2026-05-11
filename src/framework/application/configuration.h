#pragma once

#include <glaze/glaze.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct LogRotationConfiguration
{
    std::string mode = "size";
    std::size_t max_size = 10 * 1024 * 1024;
    std::size_t max_files = 5;
    std::size_t daily_hour = 0;
    std::size_t daily_minute = 0;
};

struct LogConfiguration
{
    std::string file;
    std::string error_file;
    std::string level = "info";
    bool console = true;
    bool syslog = false;
    LogRotationConfiguration rotate;
};

struct RedisConfiguration
{
    std::vector<std::string> endpoints{"127.0.0.1:6379"};
    std::string password;
    std::uint32_t database = 0;
    std::string key_prefix;
};

struct MariaConfiguration
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 3306;
    std::string database;
    std::string username;
    std::string password;
    std::uint32_t pool_size = 16;
};

struct CommonConfiguration
{
    LogConfiguration log;
    std::map<std::string, RedisConfiguration> redis;
    std::map<std::string, MariaConfiguration> maria;
};

struct ListenConfiguration
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;
};

struct BaseApplicationConfiguration
{
    ListenConfiguration listen;
};

class IApplicationConfiguration
{
public:
    virtual ~IApplicationConfiguration() = default;
    virtual bool LoadFromGeneric(const glz::generic& value, std::string& error) = 0;
};

template <typename TConfiguration>
class JsonApplicationConfiguration : public IApplicationConfiguration
{
public:
    bool LoadFromGeneric(const glz::generic& value, std::string& error) override
    {
        TConfiguration configuration = static_cast<const TConfiguration&>(*this);
        const auto document = value.dump();
        if (!document)
        {
            error = glz::format_error(document.error());
            return false;
        }

        if (auto result = glz::read<glz::opts{.error_on_unknown_keys = false}>(configuration, *document))
        {
            error = glz::format_error(result, *document);
            return false;
        }

        static_cast<TConfiguration&>(*this) = std::move(configuration);
        return true;
    }
};

template <typename TConfiguration, typename... TArgs>
constexpr auto MakeApplicationConfigObject(TArgs&&... args)
{
    return glz::object("listen", &TConfiguration::listen, std::forward<TArgs>(args)...);
}

template <>
struct glz::meta<LogRotationConfiguration>
{
    using T = LogRotationConfiguration;
    static constexpr auto value = glz::object(
        "mode",
        &T::mode,
        "max_size",
        &T::max_size,
        "max_files",
        &T::max_files,
        "daily_hour",
        &T::daily_hour,
        "daily_minute",
        &T::daily_minute);
};

template <>
struct glz::meta<LogConfiguration>
{
    using T = LogConfiguration;
    static constexpr auto value = glz::object(
        "file",
        &T::file,
        "error_file",
        &T::error_file,
        "level",
        &T::level,
        "console",
        &T::console,
        "syslog",
        &T::syslog,
        "rotate",
        &T::rotate);
};

template <>
struct glz::meta<CommonConfiguration>
{
    using T = CommonConfiguration;
    static constexpr auto value = glz::object("log", &T::log, "redis", &T::redis, "maria", &T::maria);
};

template <>
struct glz::meta<ListenConfiguration>
{
    using T = ListenConfiguration;
    static constexpr auto value = glz::object("host", &T::host, "port", &T::port);
};

template <>
struct glz::meta<BaseApplicationConfiguration>
{
    using T = BaseApplicationConfiguration;
    static constexpr auto value = glz::object("listen", &T::listen);
};

template <>
struct glz::meta<RedisConfiguration>
{
    using T = RedisConfiguration;
    static constexpr auto value =
        glz::object("endpoints", &T::endpoints, "password", &T::password, "database", &T::database, "key_prefix", &T::key_prefix);
};

template <>
struct glz::meta<MariaConfiguration>
{
    using T = MariaConfiguration;
    static constexpr auto value = glz::object(
        "host",
        &T::host,
        "port",
        &T::port,
        "database",
        &T::database,
        "username",
        &T::username,
        "password",
        &T::password,
        "pool_size",
        &T::pool_size);
};

#define SOME_SERVER_APPLICATION_CONFIG(Type, ...)        \
    template <>                                          \
    struct glz::meta<Type>                               \
    {                                                    \
        using T = Type;                                  \
        static constexpr auto value =                    \
            MakeApplicationConfigObject<T>(__VA_ARGS__); \
    }
