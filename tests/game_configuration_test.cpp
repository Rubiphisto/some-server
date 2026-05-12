#include "framework/application/configuration.h"
#include "framework/storage/connectivity_probe.h"
#include "game/application.h"
#include "framework/storage/resolved_configuration.h"

#include <glaze/glaze.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void TestConfigurationLoad()
    {
        constexpr std::string_view document = R"json(
{
  "log": {
    "level": "debug"
  },
  "redis": {
    "default": {
      "endpoints": ["127.0.0.1:6379"],
      "password": "redis-pass",
      "database": 3,
      "key_prefix": "some_server:"
    }
  },
  "maria": {
    "default": {
      "host": "127.0.0.1",
      "port": 3307,
      "database": "game_data",
      "username": "game",
      "password": "secret",
      "pool_size": 24
    }
  },
  "game": {
    "instance_id": 2,
    "listen": {
      "host": "0.0.0.0",
      "port": 9101
    },
    "discovery": {
      "endpoints": ["127.0.0.1:2379"],
      "prefix": "/some_server/ipc/test",
      "lease_ttl_seconds": 8
    },
    "storage": {
      "defaults": {
        "alive_time_seconds": 7200,
        "landing_time_seconds": 120,
        "landing_min_time_seconds": 10
      },
      "datasets": {
        "player": {
          "redis": "default",
          "maria": "default",
          "redis_prefix": "player:",
          "table_prefix": "player_"
        }
      }
    }
  }
}
)json";

        CommonConfiguration common_configuration;
        GameConfiguration game_configuration;

        if (auto result = glz::read<glz::opts{.error_on_unknown_keys = false}>(common_configuration, document))
        {
            throw std::runtime_error(glz::format_error(result, document));
        }

        const auto parsed = glz::read_json<glz::generic>(document);
        Require(parsed.has_value(), "parse generic document");
        std::string error;
        Require(parsed->contains("game"), "game section should exist");
        Require(game_configuration.LoadFromGeneric((*parsed)["game"], error), "load game configuration");

        Require(common_configuration.log.level == "debug", "common log level");
        Require(common_configuration.redis.size() == 1, "redis profile count");
        Require(common_configuration.maria.size() == 1, "maria profile count");
        Require(common_configuration.redis.at("default").database == 3, "redis database");
        Require(common_configuration.redis.at("default").key_prefix == "some_server:", "redis key prefix");
        Require(common_configuration.maria.at("default").port == 3307, "maria port");
        Require(common_configuration.maria.at("default").pool_size == 24, "maria pool size");

        Require(game_configuration.instance_id == 2, "instance id");
        Require(game_configuration.listen.host == "0.0.0.0", "listen host");
        Require(game_configuration.listen.port == 9101, "listen port");
        Require(game_configuration.storage.defaults.alive_time_seconds == 7200, "storage default alive time");
        Require(game_configuration.storage.datasets.size() == 1, "dataset count");
        const auto& player = game_configuration.storage.datasets.at("player");
        Require(player.redis == "default", "dataset redis reference");
        Require(player.maria == "default", "dataset maria reference");
        Require(player.redis_prefix == "player:", "dataset redis prefix");
        Require(player.table_prefix == "player_", "dataset table prefix");
        Require(player.timing.landing_time_seconds == 0, "dataset timing remains unset when omitted");
    }

    void TestStorageResolve()
    {
        CommonConfiguration common_configuration;
        common_configuration.redis["default"] = RedisConfiguration{
            .endpoints = {"127.0.0.1:6379"},
            .password = "redis-pass",
            .database = 2,
            .key_prefix = "some_server:"};
        common_configuration.maria["default"] = MariaConfiguration{
            .host = "127.0.0.1",
            .port = 3306,
            .database = "game_data",
            .username = "game",
            .password = "secret",
            .pool_size = 8};

        some_server::storage::StorageConfiguration storage_configuration;
        storage_configuration.defaults = {7200, 120, 10};
        storage_configuration.datasets["player"] = some_server::storage::DatasetConfiguration{
            .redis = "default",
            .maria = "default",
            .redis_prefix = "player:",
            .table_prefix = "player_",
            .timing = {0, 30, 0}};

        some_server::storage::ResolvedStorageConfiguration resolved;
        std::string error;
        Require(
            some_server::storage::ResolveStorageConfiguration(common_configuration, storage_configuration, resolved, error),
            "resolve storage configuration");
        Require(resolved.Size() == 1, "resolved dataset count");
        const auto* player = resolved.FindDataset("player");
        Require(player != nullptr, "resolved player dataset");
        Require(player->redis != nullptr, "resolved redis profile");
        Require(player->maria != nullptr, "resolved maria profile");
        Require(player->full_redis_prefix == "some_server:player:", "full redis prefix");
        Require(player->timing.alive_time_seconds == 7200, "resolved alive time default");
        Require(player->timing.landing_time_seconds == 30, "resolved landing time override");
        Require(player->timing.landing_min_time_seconds == 10, "resolved landing min time default");
    }

    void TestStorageResolveFailsOnUnknownReference()
    {
        CommonConfiguration common_configuration;
        common_configuration.redis["default"] = RedisConfiguration{};
        common_configuration.maria["default"] = MariaConfiguration{};

        some_server::storage::StorageConfiguration storage_configuration;
        storage_configuration.datasets["player"] = some_server::storage::DatasetConfiguration{
            .redis = "missing",
            .maria = "default",
            .redis_prefix = "player:",
            .table_prefix = "player_"};

        some_server::storage::ResolvedStorageConfiguration resolved;
        std::string error;
        Require(
            !some_server::storage::ResolveStorageConfiguration(common_configuration, storage_configuration, resolved, error),
            "resolve should fail for unknown redis reference");
        Require(error.find("unknown profile 'missing'") != std::string::npos, "unknown redis reference error");
    }

    void TestParseRedisEndpoint()
    {
        RedisConfiguration configuration;
        configuration.endpoints = {"127.0.0.1:6380"};

        some_server::storage::TcpEndpoint endpoint;
        std::string error;
        Require(some_server::storage::ParseRedisEndpoint(configuration, endpoint, error), "parse redis endpoint");
        Require(endpoint.host == "127.0.0.1", "redis endpoint host");
        Require(endpoint.port == 6380, "redis endpoint port");
    }

    void TestStorageResolveFailsOnUnsafeTablePrefix()
    {
        CommonConfiguration common_configuration;
        common_configuration.redis["default"] = RedisConfiguration{};
        common_configuration.maria["default"] = MariaConfiguration{};

        some_server::storage::StorageConfiguration storage_configuration;
        storage_configuration.datasets["player"] = some_server::storage::DatasetConfiguration{
            .redis = "default",
            .maria = "default",
            .redis_prefix = "player:",
            .table_prefix = "player-unsafe"};

        some_server::storage::ResolvedStorageConfiguration resolved;
        std::string error;
        Require(
            !some_server::storage::ResolveStorageConfiguration(common_configuration, storage_configuration, resolved, error),
            "resolve should fail for unsafe table prefix");
        Require(error.find("table_prefix must contain only") != std::string::npos, "unsafe table prefix error");
    }
}

int main()
{
    try
    {
        TestConfigurationLoad();
        TestStorageResolve();
        TestStorageResolveFailsOnUnknownReference();
        TestParseRedisEndpoint();
        TestStorageResolveFailsOnUnsafeTablePrefix();
        std::cout << "game_configuration_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "game_configuration_test: " << ex.what() << std::endl;
        return 1;
    }
}
