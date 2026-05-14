#pragma once

#include "framework/application/application.h"

#include <cstdint>
#include <string>

struct SimClientTargetConfiguration
{
    std::string host = "127.0.0.1";
    std::uint32_t port = 9000;
};

struct SimClientConfiguration : public BaseApplicationConfiguration, public JsonApplicationConfiguration<SimClientConfiguration>
{
    SimClientTargetConfiguration gate;
    std::string default_platform = "dev";
    std::string default_account_id = "sim-account-1";
    std::uint32_t default_client_version = 1;
    std::string default_channel = "sim";
};

class ConnectionService;
class ProtocolService;
class ScenarioService;

class Application : public ApplicationBase<SimClientConfiguration>
{
public:
    std::string GetName() const override { return "sim_client"; }

protected:
    void RegisterServices() override;
    void RegisterRuntimeCommands() override;
    LifecycleTask OnUnload() override;
    LifecycleTask OnStart() override;
    LifecycleTask OnStop() override;
    LifecycleTask OnLoad() override;

private:
    ConnectionService* mConnectionService = nullptr;
    ProtocolService* mProtocolService = nullptr;
    ScenarioService* mScenarioService = nullptr;
};

SOME_SERVER_APPLICATION_CONFIG(
    SimClientConfiguration,
    "gate",
    &SimClientConfiguration::gate,
    "default_platform",
    &SimClientConfiguration::default_platform,
    "default_account_id",
    &SimClientConfiguration::default_account_id,
    "default_client_version",
    &SimClientConfiguration::default_client_version,
    "default_channel",
    &SimClientConfiguration::default_channel);

template <>
struct glz::meta<SimClientTargetConfiguration>
{
    using T = SimClientTargetConfiguration;
    static constexpr auto value = glz::object("host", &T::host, "port", &T::port);
};
