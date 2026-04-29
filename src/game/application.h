#pragma once

#include "framework/application/application.h"

#include <cstdint>
#include <string>
#include <vector>

struct GameDiscoveryConfiguration
{
    std::string backend = "etcdctl";
    std::vector<std::string> endpoints{"127.0.0.1:2379"};
    std::string prefix = "/some_server/ipc/dev/local";
    std::uint32_t lease_ttl_seconds = 5;
};

struct GameConfiguration : public BaseApplicationConfiguration, public JsonApplicationConfiguration<GameConfiguration>
{
    std::uint32_t instance_id = 1;
    GameDiscoveryConfiguration discovery;
};

class GameIpcClientService;

class Application : public ApplicationBase<GameConfiguration>
{
public:
    std::string GetName() const override { return "game"; }

protected:
    void RegisterServices() override;
    void RegisterRuntimeCommands() override;
    LifecycleTask OnUnload() override;
    LifecycleTask OnStart() override;
    LifecycleTask OnStop() override;
    LifecycleTask OnLoad() override;

private:
    GameIpcClientService* mIpcService = nullptr;
};

SOME_SERVER_APPLICATION_CONFIG(
    GameConfiguration,
    "instance_id",
    &GameConfiguration::instance_id,
    "discovery",
    &GameConfiguration::discovery);

template <>
struct glz::meta<GameDiscoveryConfiguration>
{
    using T = GameDiscoveryConfiguration;
    static constexpr auto value = glz::object(
        "backend",
        &T::backend,
        "endpoints",
        &T::endpoints,
        "prefix",
        &T::prefix,
        "lease_ttl_seconds",
        &T::lease_ttl_seconds);
};
