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
                "game ipc status: service_type={} instance_id={} transport_ready={} registered={} ipc_ready={} keepalive_running={} members={} process_dispatch_count={} last_process_payload_type={} local_service_dispatch_count={} last_payload_type={} last_error={}",
                status.self.process.process_id.service_type,
                status.self.process.process_id.instance_id,
                status.transport_ready,
                status.registered,
                status.ipc_ready,
                status.keepalive_running,
                status.member_count,
                status.process_dispatch_count,
                status.last_process_payload_type.empty() ? "none" : status.last_process_payload_type,
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
