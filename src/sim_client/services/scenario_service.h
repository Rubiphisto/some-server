#pragma once

#include "../../framework/application/service_base.h"
#include "../../framework/ipc/base/result.h"

#include <cstdint>
#include <mutex>
#include <string>

class ConnectionService;
class ProtocolService;

struct SimClientScenarioSnapshot
{
    std::string last_scenario;
    std::uint64_t run_count = 0;
};

class ScenarioService final : public ServiceBase
{
public:
    static constexpr const char* kConnectOnlyScenario = "connect_only";
    static constexpr const char* kLoginSmokeScenario = "login_smoke";
    static constexpr const char* kPlayerEchoScenario = "player_echo";
    static constexpr const char* kPlayerRenameScenario = "player_rename";
    static constexpr const char* kReconnectAfterDisconnectScenario = "reconnect_after_disconnect";
    static constexpr const char* kRecoverAfterKickScenario = "recover_after_kick";
    static constexpr const char* kLoginThenEchoScenario = "login_then_echo";

    ScenarioService(ConnectionService* connection_service, ProtocolService* protocol_service);

    ipc::Result Run(std::string_view scenario_name, std::string_view argument = {});
    SimClientScenarioSnapshot Snapshot() const;

private:
    ConnectionService* mConnectionService = nullptr;
    ProtocolService* mProtocolService = nullptr;
    mutable std::mutex mMutex;
    SimClientScenarioSnapshot mSnapshot;
};
