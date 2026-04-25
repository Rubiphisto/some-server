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
                "game ipc status: service_type={} instance_id={} transport_ready={} registered={} ipc_ready={} keepalive_running={} members={} last_error={}",
                status.self.process.process_id.service_type,
                status.self.process.process_id.instance_id,
                status.transport_ready,
                status.registered,
                status.ipc_ready,
                status.keepalive_running,
                status.member_count,
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
