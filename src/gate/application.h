#pragma once

#include "framework/application/application.h"

struct GateDiscoveryConfiguration
{
    std::vector<std::string> endpoints{"127.0.0.1:2379"};
    std::string prefix = "/some_server/ipc/dev/local";
    std::uint32_t lease_ttl_seconds = 5;
};

struct GateClientListenConfiguration
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;
};

struct GateConfiguration : public BaseApplicationConfiguration, public JsonApplicationConfiguration<GateConfiguration>
{
    std::uint32_t instance_id = 1;
    GateClientListenConfiguration client_listen;
    GateDiscoveryConfiguration discovery;
};

class GateIpcService;
class GateAccountDirectoryService;
class GateLoginService;
class GatePlayerMessageService;
class GateConnectionService;
class GateSessionService;
class GateRoutingService;
class GateProtocolService;
class GateAuthService;
class Application : public ApplicationBase<GateConfiguration>
{
public:
    std::string GetName() const override { return "gate"; }

protected:
    void RegisterServices() override;
    void RegisterRuntimeCommands() override;
    LifecycleTask OnUnload() override;
    LifecycleTask OnStart() override;
    LifecycleTask OnStop() override;
    LifecycleTask OnLoad() override;

private:
    GateIpcService* mIpcService = nullptr;
    GateAccountDirectoryService* mAccountDirectoryService = nullptr;
    GateConnectionService* mConnectionService = nullptr;
    GateSessionService* mSessionService = nullptr;
    GateRoutingService* mRoutingService = nullptr;
    GateProtocolService* mProtocolService = nullptr;
    GateAuthService* mAuthService = nullptr;
    GateLoginService* mLoginService = nullptr;
    GatePlayerMessageService* mPlayerMessageService = nullptr;
};

SOME_SERVER_APPLICATION_CONFIG(
    GateConfiguration,
    "instance_id",
    &GateConfiguration::instance_id,
    "client_listen",
    &GateConfiguration::client_listen,
    "discovery",
    &GateConfiguration::discovery);

template <>
struct glz::meta<GateClientListenConfiguration>
{
    using T = GateClientListenConfiguration;
    static constexpr auto value = glz::object("host", &T::host, "port", &T::port);
};

template <>
struct glz::meta<GateDiscoveryConfiguration>
{
    using T = GateDiscoveryConfiguration;
    static constexpr auto value = glz::object(
        "endpoints",
        &T::endpoints,
        "prefix",
        &T::prefix,
        "lease_ttl_seconds",
        &T::lease_ttl_seconds);
};
