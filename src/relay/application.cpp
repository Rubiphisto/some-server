#include "application.h"

#include "services/ipc_service.h"

#include <spdlog/spdlog.h>

#include <stdexcept>

namespace
{
constexpr ipc::ServiceType kRelayServiceType = 99;

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
    auto service = std::make_unique<RelayIpcService>(AppConfig(), kRelayServiceType);
    mIpcService = service.get();
    AddService(std::move(service));
}

void Application::RegisterRuntimeCommands()
{
    const bool status_registered = Runtime().RegisterCommand(
        "ipc_status",
        "Show relay IPC bootstrap status",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("relay ipc status: service not registered");
                return CommandExecutionStatus::handled;
            }

            const RelayIpcStatus status = mIpcService->Snapshot();
            spdlog::info(
                "relay ipc status: service_type={} instance_id={} transport_ready={} registered={} ipc_ready={} membership_degraded={} keepalive_running={} watch_running={} members={} visible_game_members={} healthy_game_links={} auto_connect_targets={} auto_connect_success_count={} auto_connect_failure_count={} last_auto_connect_target={}:{} last_auto_connect_failure_target={}:{} last_auto_connect_failure_reason={} forwarded_data_frame_count={} last_error={}",
                status.self.process.process_id.service_type,
                status.self.process.process_id.instance_id,
                status.transport_ready,
                status.registered,
                status.ipc_ready,
                status.membership_degraded,
                status.keepalive_running,
                status.watch_running,
                status.member_count,
                status.visible_game_members,
                status.healthy_game_links,
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
                status.forwarded_data_frame_count,
                status.last_error.empty() ? "none" : status.last_error);
            return CommandExecutionStatus::handled;
        });

    if (!status_registered)
    {
        throw std::runtime_error("failed to register relay ipc status command");
    }

    const bool refresh_registered = Runtime().RegisterCommand(
        "ipc_refresh",
        "Refresh relay IPC discovery snapshot",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("relay ipc refresh: service not registered");
                return CommandExecutionStatus::handled;
            }

            const ipc::Result refresh_result = mIpcService->RefreshDiscovery();
            if (!refresh_result.ok)
            {
                spdlog::warn("relay ipc refresh failed: {}", refresh_result.message);
                return CommandExecutionStatus::handled;
            }

            const auto events = mIpcService->DrainMembershipEvents();
            spdlog::info("relay ipc refresh: members={} events={}", mIpcService->Members().size(), events.size());
            for (const auto& event : events)
            {
                spdlog::info(
                    "relay ipc event: type={} service_type={} instance_id={} incarnation={}",
                    ToString(event.type),
                    event.process.process.process_id.service_type,
                    event.process.process.process_id.instance_id,
                    event.process.process.incarnation_id);
            }
            return CommandExecutionStatus::handled;
        });

    if (!refresh_registered)
    {
        throw std::runtime_error("failed to register relay ipc refresh command");
    }

    const bool keepalive_registered = Runtime().RegisterCommand(
        "ipc_keepalive",
        "Send one relay IPC discovery lease keepalive",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("relay ipc keepalive: service not registered");
                return CommandExecutionStatus::handled;
            }

            const ipc::Result keepalive_result = mIpcService->KeepAliveOnce();
            if (!keepalive_result.ok)
            {
                spdlog::warn("relay ipc keepalive failed: {}", keepalive_result.message);
                return CommandExecutionStatus::handled;
            }

            spdlog::info("relay ipc keepalive: ok");
            return CommandExecutionStatus::handled;
        });

    if (!keepalive_registered)
    {
        throw std::runtime_error("failed to register relay ipc keepalive command");
    }

    const bool members_registered = Runtime().RegisterCommand(
        "ipc_members",
        "List relay IPC discovery members",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("relay ipc members: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto members = mIpcService->Members();
            spdlog::info("relay ipc members: count={}", members.size());
            for (const auto& member : members)
            {
                spdlog::info(
                    "relay ipc member: service={} service_type={} instance_id={} host={} port={}",
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
        throw std::runtime_error("failed to register relay ipc members command");
    }

    const bool topology_registered = Runtime().RegisterCommand(
        "ipc_topology",
        "Show relay IPC topology and auto-connect state",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("relay ipc topology: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto status = mIpcService->Snapshot();
            const auto links = mIpcService->HealthyLinks();
            spdlog::info(
                "relay ipc topology: visible_game_members={} healthy_game_links={} auto_connect_targets={} auto_connect_success_count={} auto_connect_failure_count={} last_auto_connect_target={}:{} last_auto_connect_failure_target={}:{} last_auto_connect_failure_reason={} forwarded_data_frame_count={}",
                status.visible_game_members,
                status.healthy_game_links,
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
                status.forwarded_data_frame_count);
            for (const auto& link : links)
            {
                spdlog::info(
                    "relay ipc topology link: service_type={} instance_id={} incarnation={} role={}",
                    link.process_id.service_type,
                    link.process_id.instance_id,
                    link.incarnation_id,
                    link.process_id.service_type == kRelayServiceType ? "relay" : "game");
            }
            return CommandExecutionStatus::handled;
        });

    if (!topology_registered)
    {
        throw std::runtime_error("failed to register relay ipc topology command");
    }

    const bool links_registered = Runtime().RegisterCommand(
        "ipc_links",
        "List relay IPC healthy direct links",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("relay ipc links: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto links = mIpcService->HealthyLinks();
            spdlog::info("relay ipc links: count={}", links.size());
            for (const auto& link : links)
            {
                spdlog::info(
                    "relay ipc link: service_type={} instance_id={} incarnation={}",
                    link.process_id.service_type,
                    link.process_id.instance_id,
                    link.incarnation_id);
            }
            return CommandExecutionStatus::handled;
        });

    if (!links_registered)
    {
        throw std::runtime_error("failed to register relay ipc links command");
    }

    const bool connect_registered = Runtime().RegisterCommand(
        "ipc_connect",
        "Connect relay to another process by service_type and instance_id",
        [this](const CommandArguments& arguments) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("relay ipc connect: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 2)
            {
                spdlog::warn("usage: ipc_connect <service_type> <instance_id>");
                return CommandExecutionStatus::handled;
            }

            const auto service_type = static_cast<ipc::ServiceType>(std::stoul(arguments[0]));
            const auto instance_id = static_cast<ipc::InstanceId>(std::stoul(arguments[1]));
            const ipc::Result connect_result = mIpcService->ConnectToMember(service_type, instance_id);
            if (!connect_result.ok)
            {
                spdlog::warn("relay ipc connect failed: {}", connect_result.message);
                return CommandExecutionStatus::handled;
            }

            spdlog::info("relay ipc connect: ok");
            return CommandExecutionStatus::handled;
        });

    if (!connect_registered)
    {
        throw std::runtime_error("failed to register relay ipc connect command");
    }
}
