#pragma once

#include "framework/application/application.h"

#include <cstdint>
#include <string>
#include <vector>

struct RelayDiscoveryConfiguration
{
    std::string backend = "etcdctl";
    std::vector<std::string> endpoints{"127.0.0.1:2379"};
    std::string prefix = "/some_server/ipc/dev/local";
    std::uint32_t lease_ttl_seconds = 5;
};

struct RelayConfiguration : public BaseApplicationConfiguration, public JsonApplicationConfiguration<RelayConfiguration>
{
    std::uint32_t instance_id = 1;
    RelayDiscoveryConfiguration discovery;
};

class RelayIpcService;

class Application : public ApplicationBase<RelayConfiguration>
{
public:
    std::string GetName() const override { return "relay"; }

protected:
    void RegisterServices() override;
    void RegisterRuntimeCommands() override;

private:
    class RelayIpcService* mIpcService = nullptr;
};

SOME_SERVER_APPLICATION_CONFIG(
    RelayConfiguration,
    "instance_id",
    &RelayConfiguration::instance_id,
    "discovery",
    &RelayConfiguration::discovery);

template <>
struct glz::meta<RelayDiscoveryConfiguration>
{
    using T = RelayDiscoveryConfiguration;
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
