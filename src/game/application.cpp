#include "application.h"

#include "services/ipc_client_service.h"

#include <spdlog/spdlog.h>

#include <stdexcept>

namespace
{
constexpr ipc::ServiceType kGameServiceType = 10;

const char* ToString(const ipc::MembershipEventType type)
{
    switch (type)
    {
    case ipc::MembershipEventType::added:
        return "added";
    case ipc::MembershipEventType::updated:
        return "updated";
    case ipc::MembershipEventType::removed:
        return "removed";
    }
    return "unknown";
}
}

void Application::RegisterServices()
{
    auto service = std::make_unique<GameIpcClientService>(AppConfig(), kGameServiceType);
    mIpcService = service.get();
    AddService(std::move(service));
}

void Application::RegisterRuntimeCommands()
{
    const bool registered = Runtime().RegisterCommand(
        "status",
        "Show game runtime status",
        [](const CommandArguments&) {
            spdlog::info("game status: {}", "running");
            return CommandExecutionStatus::handled;
        });

    if (!registered)
    {
        throw std::runtime_error("failed to register game status command");
    }

    const bool ipc_status_registered = Runtime().RegisterCommand(
        "ipc_status",
        "Show game IPC bootstrap status",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc status: service not registered");
                return CommandExecutionStatus::handled;
            }

            const GameIpcClientStatus status = mIpcService->Snapshot();
            spdlog::info(
                "game ipc status: service_type={} instance_id={} transport_ready={} registered={} ipc_ready={} membership_degraded={} keepalive_running={} watch_running={} members={} auto_connect_targets={} auto_connect_success_count={} last_auto_connect_target={}:{} process_dispatch_count={} last_process_payload_type={} player_dispatch_count={} last_player_id={} last_player_payload_type={} local_service_dispatch_count={} last_payload_type={} last_error={}",
                status.self.process.process_id.service_type,
                status.self.process.process_id.instance_id,
                status.transport_ready,
                status.registered,
                status.ipc_ready,
                status.membership_degraded,
                status.keepalive_running,
                status.watch_running,
                status.member_count,
                status.auto_connect_targets,
                status.auto_connect_success_count,
                status.has_last_auto_connect_target ? status.last_auto_connect_target.process_id.service_type : 0,
                status.has_last_auto_connect_target ? status.last_auto_connect_target.process_id.instance_id : 0,
                status.process_dispatch_count,
                status.last_process_payload_type.empty() ? "none" : status.last_process_payload_type,
                status.player_dispatch_count,
                status.last_player_id,
                status.last_player_payload_type.empty() ? "none" : status.last_player_payload_type,
                status.local_service_dispatch_count,
                status.last_payload_type.empty() ? "none" : status.last_payload_type,
                status.last_error.empty() ? "none" : status.last_error);
            return CommandExecutionStatus::handled;
        });

    if (!ipc_status_registered)
    {
        throw std::runtime_error("failed to register game ipc status command");
    }

    const bool refresh_registered = Runtime().RegisterCommand(
        "ipc_refresh",
        "Refresh game IPC discovery snapshot",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc refresh: service not registered");
                return CommandExecutionStatus::handled;
            }

            const ipc::Result refresh_result = mIpcService->RefreshDiscovery();
            if (!refresh_result.ok)
            {
                spdlog::warn("game ipc refresh failed: {}", refresh_result.message);
                return CommandExecutionStatus::handled;
            }

            const auto events = mIpcService->DrainMembershipEvents();
            spdlog::info("game ipc refresh: members={} events={}", mIpcService->Members().size(), events.size());
            for (const auto& event : events)
            {
                spdlog::info(
                    "game ipc event: type={} service_type={} instance_id={} incarnation={}",
                    ToString(event.type),
                    event.process.process.process_id.service_type,
                    event.process.process.process_id.instance_id,
                    event.process.process.incarnation_id);
            }
            return CommandExecutionStatus::handled;
        });

    if (!refresh_registered)
    {
        throw std::runtime_error("failed to register game ipc refresh command");
    }

    const bool keepalive_registered = Runtime().RegisterCommand(
        "ipc_keepalive",
        "Send one game IPC discovery lease keepalive",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc keepalive: service not registered");
                return CommandExecutionStatus::handled;
            }

            const ipc::Result keepalive_result = mIpcService->KeepAliveOnce();
            if (!keepalive_result.ok)
            {
                spdlog::warn("game ipc keepalive failed: {}", keepalive_result.message);
                return CommandExecutionStatus::handled;
            }

            spdlog::info("game ipc keepalive: ok");
            return CommandExecutionStatus::handled;
        });

    if (!keepalive_registered)
    {
        throw std::runtime_error("failed to register game ipc keepalive command");
    }

    const bool members_registered = Runtime().RegisterCommand(
        "ipc_members",
        "List game IPC discovery members",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc members: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto members = mIpcService->Members();
            spdlog::info("game ipc members: count={}", members.size());
            for (const auto& member : members)
            {
                spdlog::info(
                    "game ipc member: service={} service_type={} instance_id={} host={} port={}",
                    member.service_name,
                    member.process.process_id.service_type,
                    member.process.process_id.instance_id,
                    member.listen_endpoint.host,
                    member.listen_endpoint.port);
            }
            return CommandExecutionStatus::handled;
        });

    if (!members_registered)
    {
        throw std::runtime_error("failed to register game ipc members command");
    }

    const bool receivers_registered = Runtime().RegisterCommand(
        "ipc_receivers",
        "List game local IPC receivers",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc receivers: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto receivers = mIpcService->LocalReceivers();
            spdlog::info(
                "game ipc receiver: type=process service_type={} instance_id={}",
                receivers.process_receiver.process_id.service_type,
                receivers.process_receiver.process_id.instance_id);
            spdlog::info(
                "game ipc receiver: type=service service_type={} key_lo={}",
                receivers.service_receiver.key_hi,
                receivers.service_receiver.key_lo);
            spdlog::info("game ipc receivers: local_players={}", receivers.local_player_ids.size());
            for (const auto player_id : receivers.local_player_ids)
            {
                spdlog::info("game ipc receiver: type=player player_id={}", player_id);
            }
            return CommandExecutionStatus::handled;
        });

    if (!receivers_registered)
    {
        throw std::runtime_error("failed to register game ipc receivers command");
    }

    const bool links_registered = Runtime().RegisterCommand(
        "ipc_links",
        "List game IPC healthy direct links",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc links: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto links = mIpcService->HealthyLinks();
            spdlog::info("game ipc links: count={}", links.size());
            for (const auto& link : links)
            {
                spdlog::info(
                    "game ipc link: service_type={} instance_id={} incarnation={}",
                    link.process_id.service_type,
                    link.process_id.instance_id,
                    link.incarnation_id);
            }
            return CommandExecutionStatus::handled;
        });

    if (!links_registered)
    {
        throw std::runtime_error("failed to register game ipc links command");
    }

    const bool connect_registered = Runtime().RegisterCommand(
        "ipc_connect",
        "Connect to another game process by instance id",
        [this](const CommandArguments& arguments) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc connect: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: ipc_connect <instance_id>");
                return CommandExecutionStatus::handled;
            }

            const auto instance_id = static_cast<ipc::InstanceId>(std::stoul(arguments.front()));
            const ipc::Result connect_result = mIpcService->ConnectToProcess(instance_id);
            if (!connect_result.ok)
            {
                spdlog::warn("game ipc connect failed: {}", connect_result.message);
                return CommandExecutionStatus::handled;
            }

            spdlog::info("game ipc connect: ok");
            return CommandExecutionStatus::handled;
        });

    if (!connect_registered)
    {
        throw std::runtime_error("failed to register game ipc connect command");
    }

    const bool local_send_registered = Runtime().RegisterCommand(
        "ipc_send_local",
        "Send one local IPC message to the game service receiver host",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc local send: service not registered");
                return CommandExecutionStatus::handled;
            }

            const ipc::SendResult send_result = mIpcService->SendLocalServiceMessage("local-service-ping");
            if (!send_result.ok)
            {
                spdlog::warn("game ipc local send failed: {}", send_result.message);
                return CommandExecutionStatus::handled;
            }

            spdlog::info("game ipc local send: ok");
            return CommandExecutionStatus::handled;
        });

    if (!local_send_registered)
    {
        throw std::runtime_error("failed to register game ipc local send command");
    }

    const bool process_send_registered = Runtime().RegisterCommand(
        "ipc_send_process",
        "Send one IPC process-targeted message to another game process",
        [this](const CommandArguments& arguments) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc process send: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.empty() || arguments.size() > 2)
            {
                spdlog::warn("usage: ipc_send_process <instance_id> [value]");
                return CommandExecutionStatus::handled;
            }

            const auto instance_id = static_cast<ipc::InstanceId>(std::stoul(arguments.front()));
            const std::string payload = arguments.size() == 2 ? arguments[1] : "process-ping";
            const ipc::SendResult send_result = mIpcService->SendProcessMessage(instance_id, payload);
            if (!send_result.ok)
            {
                spdlog::warn("game ipc process send failed: {}", send_result.message);
                return CommandExecutionStatus::handled;
            }

            spdlog::info("game ipc process send: ok");
            return CommandExecutionStatus::handled;
        });

    if (!process_send_registered)
    {
        throw std::runtime_error("failed to register game ipc process send command");
    }

    const bool bind_player_local_registered = Runtime().RegisterCommand(
        "ipc_bind_player_local",
        "Bind a local player receiver to this game process",
        [this](const CommandArguments& arguments) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc bind player local: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: ipc_bind_player_local <player_id>");
                return CommandExecutionStatus::handled;
            }

            const auto player_id = static_cast<std::uint64_t>(std::stoull(arguments.front()));
            const ipc::Result bind_result = mIpcService->BindLocalPlayer(player_id);
            if (!bind_result.ok)
            {
                spdlog::warn("game ipc bind player local failed: {}", bind_result.message);
                return CommandExecutionStatus::handled;
            }

            spdlog::info("game ipc bind player local: ok");
            return CommandExecutionStatus::handled;
        });

    if (!bind_player_local_registered)
    {
        throw std::runtime_error("failed to register game ipc bind player local command");
    }

    const bool bind_player_remote_registered = Runtime().RegisterCommand(
        "ipc_bind_player_remote",
        "Bind a remote player receiver to another game process in the local directory",
        [this](const CommandArguments& arguments) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc bind player remote: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 2)
            {
                spdlog::warn("usage: ipc_bind_player_remote <player_id> <instance_id>");
                return CommandExecutionStatus::handled;
            }

            const auto player_id = static_cast<std::uint64_t>(std::stoull(arguments[0]));
            const auto instance_id = static_cast<ipc::InstanceId>(std::stoul(arguments[1]));
            const ipc::Result bind_result = mIpcService->BindRemotePlayer(player_id, instance_id);
            if (!bind_result.ok)
            {
                spdlog::warn("game ipc bind player remote failed: {}", bind_result.message);
                return CommandExecutionStatus::handled;
            }

            spdlog::info("game ipc bind player remote: ok");
            return CommandExecutionStatus::handled;
        });

    if (!bind_player_remote_registered)
    {
        throw std::runtime_error("failed to register game ipc bind player remote command");
    }

    const bool player_send_registered = Runtime().RegisterCommand(
        "ipc_send_player",
        "Send one IPC player-targeted message",
        [this](const CommandArguments& arguments) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc player send: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.empty() || arguments.size() > 2)
            {
                spdlog::warn("usage: ipc_send_player <player_id> [value]");
                return CommandExecutionStatus::handled;
            }

            const auto player_id = static_cast<std::uint64_t>(std::stoull(arguments.front()));
            const std::string payload = arguments.size() == 2 ? arguments[1] : "player-ping";
            const ipc::SendResult send_result = mIpcService->SendPlayerMessage(player_id, payload);
            if (!send_result.ok)
            {
                spdlog::warn("game ipc player send failed: {}", send_result.message);
                return CommandExecutionStatus::handled;
            }

            spdlog::info("game ipc player send: ok");
            return CommandExecutionStatus::handled;
        });

    if (!player_send_registered)
    {
        throw std::runtime_error("failed to register game ipc player send command");
    }

    const bool broadcast_service_registered = Runtime().RegisterCommand(
        "ipc_broadcast_service",
        "Broadcast one IPC service-targeted message to game instances",
        [this](const CommandArguments& arguments) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc broadcast service: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() > 2)
            {
                spdlog::warn("usage: ipc_broadcast_service [value] [include_local]");
                return CommandExecutionStatus::handled;
            }

            const std::string payload = !arguments.empty() ? arguments[0] : "broadcast-ping";
            const bool include_local = arguments.size() == 2 ? arguments[1] != "0" : true;
            const ipc::SendResult send_result = mIpcService->BroadcastServiceMessage(payload, include_local);
            if (!send_result.ok)
            {
                spdlog::warn("game ipc broadcast service failed: {}", send_result.message);
                return CommandExecutionStatus::handled;
            }

            spdlog::info("game ipc broadcast service: ok");
            return CommandExecutionStatus::handled;
        });

    if (!broadcast_service_registered)
    {
        throw std::runtime_error("failed to register game ipc broadcast service command");
    }
}

LifecycleTask Application::OnLoad()
{
    spdlog::info("Application::Configure(listen={}:{})",
                 AppConfig().listen.host,
                 AppConfig().listen.port);
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
