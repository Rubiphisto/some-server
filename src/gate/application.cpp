#include "application.h"

#include "services/auth_service.h"
#include "services/account_directory_service.h"
#include "services/connection_service.h"
#include "services/ipc_service.h"
#include "services/login_service.h"
#include "services/player_message_service.h"
#include "services/client_protocol_service.h"
#include "services/routing_service.h"
#include "services/session_service.h"

#include <ipc/session.pb.h>
#include <spdlog/spdlog.h>

namespace
{
constexpr ipc::ServiceType kGateServiceType = 20;
}

void Application::RegisterServices()
{
    auto connection_service =
        std::make_unique<GateConnectionService>(AppConfig().client_listen.host, AppConfig().client_listen.port);
    mConnectionService = connection_service.get();
    AddService(std::move(connection_service));

    auto account_directory_service = std::make_unique<GateAccountDirectoryService>(CommonConfig(), "default");
    mAccountDirectoryService = account_directory_service.get();
    AddService(std::move(account_directory_service));

    auto session_service = std::make_unique<GateSessionService>();
    mSessionService = session_service.get();
    AddService(std::move(session_service));

    auto routing_service = std::make_unique<GateRoutingService>();
    mRoutingService = routing_service.get();
    AddService(std::move(routing_service));

    auto protocol_service = std::make_unique<GateClientProtocolService>();
    mProtocolService = protocol_service.get();
    AddService(std::move(protocol_service));
    mProtocolService->SetSendHandler(
        [this](const std::uint64_t connection_id, const std::uint32_t message_id, const std::string& payload) {
            if (mConnectionService == nullptr)
            {
                return ipc::Result::Failure("gate connection service is not registered");
            }
            return mConnectionService->Send(connection_id, message_id, payload);
        });

    auto auth_service = std::make_unique<GateAuthService>();
    mAuthService = auth_service.get();
    AddService(std::move(auth_service));

    auto service = std::make_unique<GateIpcService>(AppConfig(), kGateServiceType);
    mIpcService = service.get();
    AddService(std::move(service));

    auto login_service = std::make_unique<GateLoginService>(
        AppConfig().instance_id,
        mIpcService,
        mAccountDirectoryService,
        mConnectionService,
        mSessionService,
        mRoutingService,
        mProtocolService,
        mAuthService);
    mLoginService = login_service.get();
    AddService(std::move(login_service));

    auto player_message_service = std::make_unique<GatePlayerMessageService>(
        mConnectionService,
        mSessionService,
        mIpcService,
        mProtocolService);
    mPlayerMessageService = player_message_service.get();
    AddService(std::move(player_message_service));
    mConnectionService->SetMessageHandler(
        [this](const std::uint64_t connection_id, const std::uint32_t message_id, const std::string& payload) {
            if (mProtocolService == nullptr)
            {
                return;
            }
            (void)mProtocolService->DispatchClientMessage(connection_id, message_id, payload);
        });
    mConnectionService->SetDisconnectHandler(
        [this](const std::uint64_t connection_id) {
            if (mSessionService == nullptr)
            {
                return;
            }

            GateSessionRecord removed;
            const auto result = mSessionService->RemoveByConnection(connection_id, &removed);
            if (!result.ok)
            {
                return;
            }
            if (mLoginService != nullptr)
            {
                (void)mLoginService->HandleSessionDisconnected(
                    removed,
                    pb::ipc::DISCONNECT_REASON_CLIENT_CLOSED);
            }
        });

}

void Application::RegisterRuntimeCommands()
{
    Runtime().RegisterCommand(
        "status",
        "Show gate runtime status",
        [](const CommandArguments&) {
            spdlog::info("gate status: {}", "running");
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "connection_status",
        "Show one gate connection state: <connection_id>",
        [this](const CommandArguments& arguments) {
            if (mConnectionService == nullptr)
            {
                spdlog::warn("gate connection status: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: connection_status <connection_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t connection_id = 0;
            try
            {
                connection_id = std::stoull(arguments[0]);
            }
            catch (const std::exception&)
            {
                spdlog::warn("gate connection status: connection_id must be an unsigned integer");
                return CommandExecutionStatus::handled;
            }

            const auto snapshot = mConnectionService->Snapshot(connection_id);
            spdlog::info(
                "gate connection status: connection_id={} present={} state={} remote={} last_recv_ms={} last_send_ms={} heartbeat_deadline_ms={}",
                connection_id,
                snapshot.has_value(),
                snapshot.has_value() ? ToString(snapshot->state) : "none",
                snapshot.has_value() ? snapshot->remote_endpoint : "none",
                snapshot.has_value() ? snapshot->last_recv_time_ms : 0,
                snapshot.has_value() ? snapshot->last_send_time_ms : 0,
                snapshot.has_value() ? snapshot->heartbeat_deadline_ms : 0);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "connection_accept",
        "Simulate accepting one connection: <connection_id> [remote_endpoint]",
        [this](const CommandArguments& arguments) {
            if (mConnectionService == nullptr)
            {
                spdlog::warn("gate connection accept: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.empty() || arguments.size() > 2)
            {
                spdlog::warn("usage: connection_accept <connection_id> [remote_endpoint]");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t connection_id = 0;
            try
            {
                connection_id = std::stoull(arguments[0]);
            }
            catch (const std::exception&)
            {
                spdlog::warn("gate connection accept: connection_id must be an unsigned integer");
                return CommandExecutionStatus::handled;
            }

            const std::string remote_endpoint = arguments.size() == 2 ? arguments[1] : "127.0.0.1:0";
            const auto result = mConnectionService->Accept(connection_id, remote_endpoint);
            if (!result.ok)
            {
                spdlog::warn("gate connection accept failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("gate connection accept: connection_id={} remote={}", connection_id, remote_endpoint);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "connection_close",
        "Simulate closing one connection: <connection_id>",
        [this](const CommandArguments& arguments) {
            if (mConnectionService == nullptr || mSessionService == nullptr)
            {
                spdlog::warn("gate connection close: services not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: connection_close <connection_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t connection_id = 0;
            try
            {
                connection_id = std::stoull(arguments[0]);
            }
            catch (const std::exception&)
            {
                spdlog::warn("gate connection close: connection_id must be an unsigned integer");
                return CommandExecutionStatus::handled;
            }

            const auto result = mConnectionService->Close(connection_id);
            if (!result.ok)
            {
                spdlog::warn("gate connection close failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("gate connection close: connection_id={}", connection_id);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "session_status",
        "Show one gate session state: <gate_session_id>",
        [this](const CommandArguments& arguments) {
            if (mSessionService == nullptr)
            {
                spdlog::warn("gate session status: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: session_status <gate_session_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t gate_session_id = 0;
            try
            {
                gate_session_id = std::stoull(arguments[0]);
            }
            catch (const std::exception&)
            {
                spdlog::warn("gate session status: gate_session_id must be an unsigned integer");
                return CommandExecutionStatus::handled;
            }

            const auto snapshot = mSessionService->Snapshot(gate_session_id);
            spdlog::info(
                "gate session status: gate_session_id={} present={} state={} connection_id={} account_id={} player_id={} game_process={}:{} login_version={} session_epoch={} last_active_ms={}",
                gate_session_id,
                snapshot.has_value(),
                snapshot.has_value() ? ToString(snapshot->state) : "none",
                snapshot.has_value() ? snapshot->connection_id : 0,
                snapshot.has_value() ? snapshot->account_id : "none",
                snapshot.has_value() ? snapshot->player_id : 0,
                snapshot.has_value() ? snapshot->game_service_type : 0,
                snapshot.has_value() ? snapshot->game_instance_id : 0,
                snapshot.has_value() ? snapshot->login_version : 0,
                snapshot.has_value() ? snapshot->session_epoch : 0,
                snapshot.has_value() ? snapshot->last_active_time_ms : 0);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "session_open",
        "Open one anonymous session: <gate_session_id> <connection_id>",
        [this](const CommandArguments& arguments) {
            if (mSessionService == nullptr)
            {
                spdlog::warn("gate session open: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 2)
            {
                spdlog::warn("usage: session_open <gate_session_id> <connection_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t gate_session_id = 0;
            std::uint64_t connection_id = 0;
            try
            {
                gate_session_id = std::stoull(arguments[0]);
                connection_id = std::stoull(arguments[1]);
            }
            catch (const std::exception&)
            {
                spdlog::warn("gate session open: arguments must be unsigned integers");
                return CommandExecutionStatus::handled;
            }

            const auto result = mSessionService->OpenAnonymous(gate_session_id, connection_id);
            if (!result.ok)
            {
                spdlog::warn("gate session open failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("gate session open: gate_session_id={} connection_id={}", gate_session_id, connection_id);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "session_activate",
        "Activate one session: <gate_session_id> <account_id> <player_id> <game_service_type> <game_instance_id>",
        [this](const CommandArguments& arguments) {
            if (mSessionService == nullptr)
            {
                spdlog::warn("gate session activate: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 5)
            {
                spdlog::warn(
                    "usage: session_activate <gate_session_id> <account_id> <player_id> <game_service_type> <game_instance_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t gate_session_id = 0;
            std::uint64_t player_id = 0;
            std::uint32_t game_service_type = 0;
            std::uint32_t game_instance_id = 0;
            try
            {
                gate_session_id = std::stoull(arguments[0]);
                player_id = std::stoull(arguments[2]);
                game_service_type = static_cast<std::uint32_t>(std::stoul(arguments[3]));
                game_instance_id = static_cast<std::uint32_t>(std::stoul(arguments[4]));
            }
            catch (const std::exception&)
            {
                spdlog::warn("gate session activate: numeric arguments must be unsigned integers");
                return CommandExecutionStatus::handled;
            }

            const auto result = mSessionService->Activate(
                gate_session_id,
                arguments[1],
                player_id,
                game_service_type,
                game_instance_id);
            if (!result.ok)
            {
                spdlog::warn("gate session activate failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info(
                "gate session activate: gate_session_id={} account_id={} player_id={} game_process={}:{}",
                gate_session_id,
                arguments[1],
                player_id,
                game_service_type,
                game_instance_id);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "route_bind",
        "Bind one player route: <player_id> <game_service_type> <game_instance_id> <gate_session_id>",
        [this](const CommandArguments& arguments) {
            if (mRoutingService == nullptr)
            {
                spdlog::warn("gate route bind: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 4)
            {
                spdlog::warn(
                    "usage: route_bind <player_id> <game_service_type> <game_instance_id> <gate_session_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t player_id = 0;
            std::uint32_t game_service_type = 0;
            std::uint32_t game_instance_id = 0;
            std::uint64_t gate_session_id = 0;
            try
            {
                player_id = std::stoull(arguments[0]);
                game_service_type = static_cast<std::uint32_t>(std::stoul(arguments[1]));
                game_instance_id = static_cast<std::uint32_t>(std::stoul(arguments[2]));
                gate_session_id = std::stoull(arguments[3]);
            }
            catch (const std::exception&)
            {
                spdlog::warn("gate route bind: arguments must be unsigned integers");
                return CommandExecutionStatus::handled;
            }

            const auto result =
                mRoutingService->BindPlayer(player_id, game_service_type, game_instance_id, gate_session_id);
            if (!result.ok)
            {
                spdlog::warn("gate route bind failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info(
                "gate route bind: player_id={} game_process={}:{} gate_session_id={}",
                player_id,
                game_service_type,
                game_instance_id,
                gate_session_id);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "route_status",
        "Show one player route: <player_id>",
        [this](const CommandArguments& arguments) {
            if (mRoutingService == nullptr)
            {
                spdlog::warn("gate route status: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: route_status <player_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t player_id = 0;
            try
            {
                player_id = std::stoull(arguments[0]);
            }
            catch (const std::exception&)
            {
                spdlog::warn("gate route status: player_id must be an unsigned integer");
                return CommandExecutionStatus::handled;
            }

            const auto snapshot = mRoutingService->Snapshot(player_id);
            spdlog::info(
                "gate route status: player_id={} present={} game_process={}:{} gate_session_id={} last_update_ms={}",
                player_id,
                snapshot.has_value(),
                snapshot.has_value() ? snapshot->game_service_type : 0,
                snapshot.has_value() ? snapshot->game_instance_id : 0,
                snapshot.has_value() ? snapshot->gate_session_id : 0,
                snapshot.has_value() ? snapshot->last_update_ms : 0);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "login_validate",
        "Validate one simulated login: <platform> <account_id> <credential>",
        [this](const CommandArguments& arguments) {
            if (mAuthService == nullptr)
            {
                spdlog::warn("gate login validate: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 3)
            {
                spdlog::warn("usage: login_validate <platform> <account_id> <credential>");
                return CommandExecutionStatus::handled;
            }

            const auto result = mAuthService->Validate(arguments[0], arguments[1], arguments[2]);
            spdlog::info(
                "gate login validate: ok={} account_id={} message={}",
                result.ok,
                result.account_id,
                result.message);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "login_request",
        "Send one simulated login request to game: <game_instance_id> <gate_session_id> <platform> <account_id> <area_id>",
        [this](const CommandArguments& arguments) {
            if (mLoginService == nullptr)
            {
                spdlog::warn("gate login request: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 5)
            {
                spdlog::warn(
                    "usage: login_request <game_instance_id> <gate_session_id> <platform> <account_id> <area_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint32_t game_instance_id = 0;
            std::uint64_t gate_session_id = 0;
            std::uint32_t area_id = 0;
            try
            {
                game_instance_id = static_cast<std::uint32_t>(std::stoul(arguments[0]));
                gate_session_id = std::stoull(arguments[1]);
                area_id = static_cast<std::uint32_t>(std::stoul(arguments[4]));
            }
            catch (const std::exception&)
            {
                spdlog::warn("gate login request: numeric arguments must be unsigned integers");
                return CommandExecutionStatus::handled;
            }

            const auto result =
                mLoginService->RequestLogin(game_instance_id, 0, gate_session_id, arguments[2], arguments[3], area_id);
            if (!result.ok)
            {
                spdlog::warn("gate login request failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info(
                "gate login request: game_instance_id={} gate_session_id={} account_id={} area_id={}",
                game_instance_id,
                gate_session_id,
                arguments[3],
                area_id);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "login_response_status",
        "Show last received simulated login response",
        [this](const CommandArguments&) {
            if (mLoginService == nullptr)
            {
                spdlog::warn("gate login response status: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto snapshot = mLoginService->Snapshot();
            spdlog::info(
                "gate login response status: request_id={} player_id={} result_code={} game_process={}:{} is_reconnect={} error_message={}",
                snapshot.last_request_id,
                snapshot.last_player_id,
                snapshot.last_result_code,
                snapshot.last_game_service_type,
                snapshot.last_game_instance_id,
                snapshot.last_is_reconnect,
                snapshot.last_error_message.empty() ? "none" : snapshot.last_error_message);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "ipc_status",
        "Show gate IPC runtime status",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("gate ipc status: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto status = mIpcService->Snapshot();
            spdlog::info(
                "gate ipc status: service_type={} instance_id={} transport_ready={} registered={} ipc_ready={} membership_degraded={} keepalive_running={} watch_running={} members={} relay_member_visible={} healthy_relay_link={} auto_connect_targets={} auto_connect_success_count={} auto_connect_failure_count={} last_auto_connect_target={}:{} last_auto_connect_failure_target={}:{} last_auto_connect_failure_reason={} process_dispatch_count={} last_process_payload_type={} local_service_dispatch_count={} last_payload_type={} last_error={}",
                status.self.process.process_id.service_type,
                status.self.process.process_id.instance_id,
                status.transport_ready,
                status.registered,
                status.ipc_ready,
                status.membership_degraded,
                status.keepalive_running,
                status.watch_running,
                status.member_count,
                status.relay_member_visible,
                status.healthy_relay_link,
                status.auto_connect_targets,
                status.auto_connect_success_count,
                status.auto_connect_failure_count,
                status.has_last_auto_connect_target ? status.last_auto_connect_target.process_id.service_type : 0,
                status.has_last_auto_connect_target ? status.last_auto_connect_target.process_id.instance_id : 0,
                status.has_last_auto_connect_failure_target
                    ? status.last_auto_connect_failure_target.process_id.service_type
                    : 0,
                status.has_last_auto_connect_failure_target
                    ? status.last_auto_connect_failure_target.process_id.instance_id
                    : 0,
                status.last_auto_connect_failure_reason.empty() ? "none" : status.last_auto_connect_failure_reason,
                status.process_dispatch_count,
                status.last_process_payload_type.empty() ? "none" : status.last_process_payload_type,
                status.local_service_dispatch_count,
                status.last_payload_type.empty() ? "none" : status.last_payload_type,
                status.last_error.empty() ? "none" : status.last_error);
            return CommandExecutionStatus::handled;
        });
}

LifecycleTask Application::OnLoad()
{
    spdlog::info("Application::Configure(ipc_listen={}:{})",
                 AppConfig().listen.host,
                 AppConfig().listen.port);
    spdlog::info(
        "Application::Configure(client_listen={}:{})",
        AppConfig().client_listen.host,
        AppConfig().client_listen.port);
    spdlog::info("Application::Load()");
    return LifecycleTask::Completed();
}

LifecycleTask Application::OnStart()
{
    spdlog::info("Application::OnStart()");
    return LifecycleTask::Completed();
}

LifecycleTask Application::OnStop()
{
    spdlog::info("Application::OnStop()");
    return LifecycleTask::Completed();
}

LifecycleTask Application::OnUnload()
{
    spdlog::info("Application::OnUnload()");
    return LifecycleTask::Completed();
}
