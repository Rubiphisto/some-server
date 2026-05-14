#include "scenario_service.h"

#include "connection_service.h"
#include "protocol_service.h"

ScenarioService::ScenarioService(ConnectionService* connection_service, ProtocolService* protocol_service)
    : ServiceBase("sim_client_scenario", 30)
    , mConnectionService(connection_service)
    , mProtocolService(protocol_service)
{
}

ipc::Result ScenarioService::Run(const std::string_view scenario_name, const std::string_view argument)
{
    if (mConnectionService == nullptr || mProtocolService == nullptr)
    {
        return ipc::Result::Failure("sim_client scenario dependencies are not registered");
    }

    if (!argument.empty())
    {
        if (const auto set_account = mProtocolService->SetAccountId(argument); !set_account.ok)
        {
            return set_account;
        }
    }

    if (scenario_name == kConnectOnlyScenario)
    {
        const auto result = mConnectionService->Connect();
        if (!result.ok)
        {
            return result;
        }
    }
    else if (scenario_name == kLoginSmokeScenario)
    {
        if (const auto connect = mConnectionService->Connect(); !connect.ok && connect.message != "socket already connected")
        {
            return connect;
        }
        const auto login = mProtocolService->BuildDefaultLoginFrame();
        if (!login.has_value())
        {
            return ipc::Result::Failure("failed to build login frame");
        }
        if (const auto send = mConnectionService->SendFrame(login->message_id, login->payload); !send.ok)
        {
            return send;
        }
        const auto heartbeat = mProtocolService->BuildHeartbeatFrame();
        if (!heartbeat.has_value())
        {
            return ipc::Result::Failure("failed to build heartbeat frame");
        }
        if (const auto send = mConnectionService->SendFrame(heartbeat->message_id, heartbeat->payload); !send.ok)
        {
            return send;
        }
    }
    else if (scenario_name == kReconnectAfterDisconnectScenario)
    {
        if (const auto connect = mConnectionService->Connect(); !connect.ok && connect.message != "socket already connected")
        {
            return connect;
        }
        if (const auto reset = mProtocolService->ResetRuntimeState(); !reset.ok)
        {
            return reset;
        }
        const auto login = mProtocolService->BuildDefaultLoginFrame();
        if (!login.has_value())
        {
            return ipc::Result::Failure("failed to build login frame");
        }
        if (const auto send = mConnectionService->SendFrame(login->message_id, login->payload); !send.ok)
        {
            return send;
        }
        if (const auto disconnect = mConnectionService->Disconnect(); !disconnect.ok)
        {
            return disconnect;
        }
        if (const auto reconnect = mConnectionService->Connect(); !reconnect.ok)
        {
            return reconnect;
        }
        const auto relogin = mProtocolService->BuildDefaultLoginFrame();
        if (!relogin.has_value())
        {
            return ipc::Result::Failure("failed to build relogin frame");
        }
        if (const auto send = mConnectionService->SendFrame(relogin->message_id, relogin->payload); !send.ok)
        {
            return send;
        }
    }
    else if (scenario_name == kRecoverAfterKickScenario)
    {
        if (const auto disconnect = mConnectionService->Disconnect(); !disconnect.ok && disconnect.message != "socket is not connected")
        {
            return disconnect;
        }
        if (const auto reconnect = mConnectionService->Connect(); !reconnect.ok)
        {
            return reconnect;
        }
        const auto relogin = mProtocolService->BuildDefaultLoginFrame();
        if (!relogin.has_value())
        {
            return ipc::Result::Failure("failed to build relogin frame");
        }
        if (const auto send = mConnectionService->SendFrame(relogin->message_id, relogin->payload); !send.ok)
        {
            return send;
        }
    }
    else if (scenario_name == kLoginThenEchoScenario)
    {
        if (const auto connect = mConnectionService->Connect(); !connect.ok && connect.message != "socket already connected")
        {
            return connect;
        }
        const auto login = mProtocolService->BuildDefaultLoginFrame();
        if (!login.has_value())
        {
            return ipc::Result::Failure("failed to build login frame");
        }
        if (const auto send = mConnectionService->SendFrame(login->message_id, login->payload); !send.ok)
        {
            return send;
        }
        const auto request = mProtocolService->BuildEchoFrame("scenario-login-echo");
        if (!request.has_value())
        {
            return ipc::Result::Failure("failed to build player message frame");
        }
        if (const auto send = mConnectionService->SendFrame(request->message_id, request->payload); !send.ok)
        {
            return send;
        }
    }
    else if (scenario_name == kPlayerEchoScenario)
    {
        if (const auto connect = mConnectionService->Connect(); !connect.ok && connect.message != "socket already connected")
        {
            return connect;
        }
        const auto request = mProtocolService->BuildEchoFrame("sim-player-ping");
        if (!request.has_value())
        {
            return ipc::Result::Failure("failed to build player message frame");
        }
        if (const auto send = mConnectionService->SendFrame(request->message_id, request->payload); !send.ok)
        {
            return send;
        }
    }
    else if (scenario_name == kPlayerRenameScenario)
    {
        if (const auto connect = mConnectionService->Connect(); !connect.ok && connect.message != "socket already connected")
        {
            return connect;
        }
        const auto request = mProtocolService->BuildRenameFrame("sim-player-renamed");
        if (!request.has_value())
        {
            return ipc::Result::Failure("failed to build rename player frame");
        }
        if (const auto send = mConnectionService->SendFrame(request->message_id, request->payload); !send.ok)
        {
            return send;
        }
    }
    else
    {
        return ipc::Result::Failure("unsupported scenario");
    }

    std::scoped_lock lock(mMutex);
    mSnapshot.last_scenario = scenario_name;
    ++mSnapshot.run_count;
    return ipc::Result::Success();
}

SimClientScenarioSnapshot ScenarioService::Snapshot() const
{
    std::scoped_lock lock(mMutex);
    return mSnapshot;
}
