#include "connectivity_probe.h"

#include <hiredis/hiredis.h>
#include <mariadb/mysql.h>

namespace some_server::storage
{
namespace
{
constexpr int kProbeTimeoutMs = 500;

bool ParsePort(const std::string& text, std::uint16_t& port)
{
    try
    {
        const unsigned long value = std::stoul(text);
        if (value > 65535)
        {
            return false;
        }
        port = static_cast<std::uint16_t>(value);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
}

bool ParseRedisEndpoint(const RedisConfiguration& configuration, TcpEndpoint& endpoint, std::string& error)
{
    if (configuration.endpoints.empty())
    {
        error = "redis endpoints must not be empty";
        return false;
    }

    const std::string& value = configuration.endpoints.front();
    const std::size_t pos = value.rfind(':');
    if (pos == std::string::npos || pos == 0 || pos + 1 >= value.size())
    {
        error = "redis endpoint must be in host:port format";
        return false;
    }

    endpoint.host = value.substr(0, pos);
    if (!ParsePort(value.substr(pos + 1), endpoint.port))
    {
        error = "redis endpoint port is invalid";
        return false;
    }
    return true;
}

RedisConnectResult ConnectRedis(const RedisConfiguration& configuration)
{
    RedisConnectResult result;

    TcpEndpoint endpoint;
    if (!ParseRedisEndpoint(configuration, endpoint, result.error))
    {
        return result;
    }

    result.target = endpoint.host + ":" + std::to_string(endpoint.port);
    const timeval timeout{
        .tv_sec = kProbeTimeoutMs / 1000,
        .tv_usec = static_cast<suseconds_t>((kProbeTimeoutMs % 1000) * 1000)};
    redisContext* context = redisConnectWithTimeout(endpoint.host.c_str(), endpoint.port, timeout);
    if (context == nullptr)
    {
        result.error = "redisConnectWithTimeout returned null";
        return result;
    }
    if (context->err != 0)
    {
        result.error = context->errstr[0] != '\0' ? context->errstr : "redis connect failed";
        redisFree(context);
        return result;
    }

    auto run_command = [&](const char* command) -> redisReply* {
        return static_cast<redisReply*>(redisCommand(context, command));
    };

    if (!configuration.password.empty())
    {
        const std::string auth_command = "AUTH " + configuration.password;
        redisReply* auth = run_command(auth_command.c_str());
        if (auth == nullptr)
        {
            result.error = context->err != 0 && context->errstr[0] != '\0' ? context->errstr : "redis AUTH failed";
            redisFree(context);
            return result;
        }
        if (auth->type == REDIS_REPLY_ERROR)
        {
            result.error = auth->str != nullptr ? auth->str : "redis AUTH error";
            freeReplyObject(auth);
            redisFree(context);
            return result;
        }
        freeReplyObject(auth);
    }

    if (configuration.database != 0)
    {
        const std::string select_command = "SELECT " + std::to_string(configuration.database);
        redisReply* select = run_command(select_command.c_str());
        if (select == nullptr)
        {
            result.error = context->err != 0 && context->errstr[0] != '\0' ? context->errstr : "redis SELECT failed";
            redisFree(context);
            return result;
        }
        if (select->type == REDIS_REPLY_ERROR)
        {
            result.error = select->str != nullptr ? select->str : "redis SELECT error";
            freeReplyObject(select);
            redisFree(context);
            return result;
        }
        freeReplyObject(select);
    }

    result.context = context;
    return result;
}

bool PingRedis(redisContext& context, std::string& error)
{
    redisReply* ping = static_cast<redisReply*>(redisCommand(&context, "PING"));
    if (ping == nullptr)
    {
        error = context.err != 0 && context.errstr[0] != '\0' ? context.errstr : "redis PING failed";
        return false;
    }
    if (ping->type == REDIS_REPLY_ERROR)
    {
        error = ping->str != nullptr ? ping->str : "redis PING error";
        freeReplyObject(ping);
        return false;
    }
    freeReplyObject(ping);
    return true;
}

void CloseRedis(redisContext* context)
{
    if (context != nullptr)
    {
        redisFree(context);
    }
}

MariaConnectResult ConnectMaria(const MariaConfiguration& configuration)
{
    MariaConnectResult result;
    result.target = configuration.host + ":" + std::to_string(configuration.port);
    if (configuration.host.empty())
    {
        result.error = "maria host must not be empty";
        return result;
    }
    if (configuration.port == 0)
    {
        result.error = "maria port must be greater than 0";
        return result;
    }

    MYSQL* mysql = mysql_init(nullptr);
    if (mysql == nullptr)
    {
        result.error = "mysql_init failed";
        return result;
    }

    const unsigned int timeout_seconds = 1;
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_seconds);

    MYSQL* connected = mysql_real_connect(
        mysql,
        configuration.host.c_str(),
        configuration.username.empty() ? nullptr : configuration.username.c_str(),
        configuration.password.empty() ? nullptr : configuration.password.c_str(),
        configuration.database.empty() ? nullptr : configuration.database.c_str(),
        configuration.port,
        nullptr,
        0);
    if (connected == nullptr)
    {
        result.error = mysql_error(mysql);
        mysql_close(mysql);
        return result;
    }

    result.handle = mysql;
    return result;
}

bool PingMaria(MYSQL& handle, std::string& error)
{
    if (mysql_ping(&handle) != 0)
    {
        error = mysql_error(&handle);
        return false;
    }
    return true;
}

void CloseMaria(MYSQL* handle)
{
    if (handle != nullptr)
    {
        mysql_close(handle);
    }
}

ConnectionProbeStatus ProbeMariaConnection(const std::string& name, const MariaConfiguration& configuration)
{
    ConnectionProbeStatus status;
    status.name = name;
    const MariaConnectResult connection = ConnectMaria(configuration);
    status.target = connection.target;
    status.error = connection.error;
    if (connection.handle == nullptr)
    {
        return status;
    }
    if (!PingMaria(*connection.handle, status.error))
    {
        CloseMaria(connection.handle);
        return status;
    }
    status.reachable = true;
    CloseMaria(connection.handle);
    return status;
}

ConnectionProbeStatus ProbeRedisConnection(const std::string& name, const RedisConfiguration& configuration)
{
    ConnectionProbeStatus status;
    status.name = name;
    const RedisConnectResult connection = ConnectRedis(configuration);
    status.target = connection.target;
    status.error = connection.error;
    if (connection.context == nullptr)
    {
        return status;
    }
    if (!PingRedis(*connection.context, status.error))
    {
        CloseRedis(connection.context);
        return status;
    }
    status.reachable = true;
    CloseRedis(connection.context);
    return status;
}
}
