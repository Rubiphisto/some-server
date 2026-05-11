#pragma once

#include "../application/configuration.h"

#include <cstdint>
#include <string>

struct redisContext;
struct st_mysql;
using MYSQL = st_mysql;

namespace some_server::storage
{
struct TcpEndpoint
{
    std::string host;
    std::uint16_t port = 0;
};

struct ConnectionProbeStatus
{
    std::string name;
    std::string target;
    bool reachable = false;
    std::string error;
};

bool ParseRedisEndpoint(const RedisConfiguration& configuration, TcpEndpoint& endpoint, std::string& error);

struct RedisConnectResult
{
    redisContext* context = nullptr;
    std::string target;
    std::string error;
};

struct MariaConnectResult
{
    MYSQL* handle = nullptr;
    std::string target;
    std::string error;
};

RedisConnectResult ConnectRedis(const RedisConfiguration& configuration);
bool PingRedis(redisContext& context, std::string& error);
void CloseRedis(redisContext* context);

MariaConnectResult ConnectMaria(const MariaConfiguration& configuration);
bool PingMaria(MYSQL& handle, std::string& error);
void CloseMaria(MYSQL* handle);

ConnectionProbeStatus ProbeRedisConnection(const std::string& name, const RedisConfiguration& configuration);
ConnectionProbeStatus ProbeMariaConnection(const std::string& name, const MariaConfiguration& configuration);
}
