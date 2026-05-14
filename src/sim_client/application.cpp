#include "application.h"

#include "services/connection_service.h"
#include "services/protocol_service.h"
#include "services/scenario_service.h"

#include <spdlog/spdlog.h>

void Application::RegisterServices()
{
    auto connection_service = std::make_unique<ConnectionService>(AppConfig().gate.host, AppConfig().gate.port);
    mConnectionService = connection_service.get();
    auto protocol_service = std::make_unique<ProtocolService>(AppConfig());
    mProtocolService = protocol_service.get();
    mConnectionService->SetMessageHandler(
        [this](const std::uint32_t message_id, const std::string& payload) {
            if (mProtocolService == nullptr)
            {
                return;
            }
            (void)mProtocolService->HandleFrame(message_id, payload);
        });
    AddService(std::move(connection_service));
    AddService(std::move(protocol_service));

    auto scenario_service = std::make_unique<ScenarioService>(mConnectionService, mProtocolService);
    mScenarioService = scenario_service.get();
    AddService(std::move(scenario_service));
}

void Application::RegisterRuntimeCommands()
{
    Runtime().RegisterCommand(
        "status",
        "Show sim_client runtime status",
        [this](const CommandArguments&) {
            if (mConnectionService == nullptr || mProtocolService == nullptr || mScenarioService == nullptr)
            {
                spdlog::warn("sim_client status: services not registered");
                return CommandExecutionStatus::handled;
            }

            const auto connection = mConnectionService->Snapshot();
            const auto protocol = mProtocolService->Snapshot();
            const auto scenario = mScenarioService->Snapshot();
            spdlog::info(
                "sim_client status: target={}:{} connected={} connect_attempts={} disconnect_count={} last_connect_ms={} last_disconnect_ms={} sent_frames={} received_frames={} configured_platform={} configured_account_id={} configured_channel={} configured_client_version={} login_message_id={} heartbeat_message_id={} kick_notification_message_id={} player_message_request_message_id={} player_message_response_message_id={} player_push_message_id={} encoded_login_bytes={} encoded_heartbeat_bytes={} encoded_player_message_bytes={} received_login_response_bytes={} last_login_player_id={} last_login_ok={} last_login_error_code={} last_login_error_message={} last_kick_reason={} last_player_response_message_id={} last_player_response_error_code={} last_player_response_error_message={} last_player_response_payload={} last_echo_text={} last_rename_display_name={} last_push_message_id={} last_push_payload={} last_profile_push_display_name={} last_scenario={} scenario_runs={}",
                connection.host,
                connection.port,
                connection.connected,
                connection.connect_attempts,
                connection.disconnect_count,
                connection.last_connect_ms,
                connection.last_disconnect_ms,
                connection.sent_frame_count,
                connection.received_frame_count,
                protocol.configured_platform.empty() ? "none" : protocol.configured_platform,
                protocol.configured_account_id.empty() ? "none" : protocol.configured_account_id,
                protocol.configured_channel.empty() ? "none" : protocol.configured_channel,
                protocol.configured_client_version,
                protocol.login_message_id,
                protocol.heartbeat_message_id,
                protocol.kick_notification_message_id,
                protocol.player_message_request_message_id,
                protocol.player_message_response_message_id,
                protocol.player_push_message_id,
                protocol.encoded_login_bytes,
                protocol.encoded_heartbeat_bytes,
                protocol.encoded_player_message_bytes,
                protocol.received_login_response_bytes,
                protocol.last_login_player_id,
                protocol.last_login_ok,
                protocol.last_login_error_code,
                protocol.last_login_error_message.empty() ? "none" : protocol.last_login_error_message,
                protocol.last_kick_reason.empty() ? "none" : protocol.last_kick_reason,
                protocol.last_player_response_message_id,
                protocol.last_player_response_error_code,
                protocol.last_player_response_error_message.empty() ? "none" : protocol.last_player_response_error_message,
                protocol.last_player_response_payload.empty() ? "none" : protocol.last_player_response_payload,
                protocol.last_echo_text.empty() ? "none" : protocol.last_echo_text,
                protocol.last_rename_display_name.empty() ? "none" : protocol.last_rename_display_name,
                protocol.last_push_message_id,
                protocol.last_push_payload.empty() ? "none" : protocol.last_push_payload,
                protocol.last_profile_push_display_name.empty() ? "none" : protocol.last_profile_push_display_name,
                scenario.last_scenario.empty() ? "none" : scenario.last_scenario,
                scenario.run_count);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "use_account",
        "Switch the default account id used by login commands: <account_id>",
        [this](const CommandArguments& arguments) {
            if (mProtocolService == nullptr)
            {
                spdlog::warn("sim_client use_account: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: use_account <account_id>");
                return CommandExecutionStatus::handled;
            }

            const auto result = mProtocolService->SetAccountId(arguments[0]);
            if (!result.ok)
            {
                spdlog::warn("sim_client use_account failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("sim_client use_account: {}", arguments[0]);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "reset_status",
        "Clear sim_client protocol runtime observations",
        [this](const CommandArguments&) {
            if (mProtocolService == nullptr)
            {
                spdlog::warn("sim_client reset_status: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto result = mProtocolService->ResetRuntimeState();
            if (!result.ok)
            {
                spdlog::warn("sim_client reset_status failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("sim_client reset_status: ok");
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "connect",
        "Connect to gate over TCP",
        [this](const CommandArguments&) {
            if (mConnectionService == nullptr)
            {
                spdlog::warn("sim_client connect: service not registered");
                return CommandExecutionStatus::handled;
            }
            const auto result = mConnectionService->Connect();
            if (!result.ok)
            {
                spdlog::warn("sim_client connect failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("sim_client connect: connected");
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "disconnect",
        "Disconnect from gate",
        [this](const CommandArguments&) {
            if (mConnectionService == nullptr)
            {
                spdlog::warn("sim_client disconnect: service not registered");
                return CommandExecutionStatus::handled;
            }
            const auto result = mConnectionService->Disconnect();
            if (!result.ok)
            {
                spdlog::warn("sim_client disconnect failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("sim_client disconnect: disconnected");
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "login",
        "Send one login request to gate: [account_id]",
        [this](const CommandArguments& arguments) {
            if (mConnectionService == nullptr || mProtocolService == nullptr)
            {
                spdlog::warn("sim_client login: services not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() > 1)
            {
                spdlog::warn("usage: login [account_id]");
                return CommandExecutionStatus::handled;
            }
            if (!arguments.empty())
            {
                const auto set_account = mProtocolService->SetAccountId(arguments[0]);
                if (!set_account.ok)
                {
                    spdlog::warn("sim_client login failed: {}", set_account.message);
                    return CommandExecutionStatus::handled;
                }
            }

            const auto frame = mProtocolService->BuildDefaultLoginFrame();
            if (!frame.has_value())
            {
                spdlog::warn("sim_client login failed: failed to build frame");
                return CommandExecutionStatus::handled;
            }
            const auto send = mConnectionService->SendFrame(frame->message_id, frame->payload);
            if (!send.ok)
            {
                spdlog::warn("sim_client login failed: {}", send.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("sim_client login: sent");
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "heartbeat",
        "Send one heartbeat request to gate",
        [this](const CommandArguments&) {
            if (mConnectionService == nullptr || mProtocolService == nullptr)
            {
                spdlog::warn("sim_client heartbeat: services not registered");
                return CommandExecutionStatus::handled;
            }

            const auto frame = mProtocolService->BuildHeartbeatFrame();
            if (!frame.has_value())
            {
                spdlog::warn("sim_client heartbeat failed: failed to build frame");
                return CommandExecutionStatus::handled;
            }
            const auto send = mConnectionService->SendFrame(frame->message_id, frame->payload);
            if (!send.ok)
            {
                spdlog::warn("sim_client heartbeat failed: {}", send.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("sim_client heartbeat: sent");
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "login_encode",
        "Encode one login request using client protobuf",
        [this](const CommandArguments&) {
            if (mProtocolService == nullptr)
            {
                spdlog::warn("sim_client login_encode: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto result = mProtocolService->EncodeDefaultLogin();
            if (!result.ok)
            {
                spdlog::warn("sim_client login_encode failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }

            spdlog::info(
                "sim_client login_encode: bytes={} message_id={}",
                result.encoded_size,
                result.message_id);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "heartbeat_encode",
        "Encode one heartbeat request using client protobuf",
        [this](const CommandArguments&) {
            if (mProtocolService == nullptr)
            {
                spdlog::warn("sim_client heartbeat_encode: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto result = mProtocolService->EncodeHeartbeat();
            if (!result.ok)
            {
                spdlog::warn("sim_client heartbeat_encode failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }

            spdlog::info(
                "sim_client heartbeat_encode: bytes={} message_id={}",
                result.encoded_size,
                result.message_id);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_echo",
        "Send one typed echo message to gate/game: [text]",
        [this](const CommandArguments& arguments) {
            if (mConnectionService == nullptr || mProtocolService == nullptr)
            {
                spdlog::warn("sim_client player_echo: services not registered");
                return CommandExecutionStatus::handled;
            }
            const std::string text = arguments.empty() ? "sim-player-ping" : arguments[0];
            const auto frame = mProtocolService->BuildEchoFrame(text);
            if (!frame.has_value())
            {
                spdlog::warn("sim_client player_echo failed: failed to build frame");
                return CommandExecutionStatus::handled;
            }
            const auto send = mConnectionService->SendFrame(frame->message_id, frame->payload);
            if (!send.ok)
            {
                spdlog::warn("sim_client player_echo failed: {}", send.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("sim_client player_echo: {}", text);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_rename",
        "Send one typed rename message to gate/game: [display_name]",
        [this](const CommandArguments& arguments) {
            if (mConnectionService == nullptr || mProtocolService == nullptr)
            {
                spdlog::warn("sim_client player_rename: services not registered");
                return CommandExecutionStatus::handled;
            }
            const std::string display_name = arguments.empty() ? "sim-player-renamed" : arguments[0];
            const auto frame = mProtocolService->BuildRenameFrame(display_name);
            if (!frame.has_value())
            {
                spdlog::warn("sim_client player_rename failed: failed to build frame");
                return CommandExecutionStatus::handled;
            }
            const auto send = mConnectionService->SendFrame(frame->message_id, frame->payload);
            if (!send.ok)
            {
                spdlog::warn("sim_client player_rename failed: {}", send.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("sim_client player_rename: {}", display_name);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "scenario_run",
        "Run one named simulated scenario",
        [this](const CommandArguments& arguments) {
            if (mScenarioService == nullptr)
            {
                spdlog::warn("sim_client scenario_run: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.empty() || arguments.size() > 2)
            {
                spdlog::warn(
                    "usage: scenario_run <connect_only|login_smoke|player_echo|player_rename|reconnect_after_disconnect|recover_after_kick|login_then_echo> [account_id]");
                return CommandExecutionStatus::handled;
            }

            const auto result =
                mScenarioService->Run(arguments[0], arguments.size() == 2 ? arguments[1] : std::string_view{});
            if (!result.ok)
            {
                spdlog::warn("sim_client scenario_run failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }

            spdlog::info("sim_client scenario_run: {}", arguments[0]);
            return CommandExecutionStatus::handled;
        });
}

LifecycleTask Application::OnLoad()
{
    spdlog::info("Application::Configure(gate={}:{})", AppConfig().gate.host, AppConfig().gate.port);
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
