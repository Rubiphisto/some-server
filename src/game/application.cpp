#include "application.h"

#include "services/ipc_client_service.h"
#include "services/player_directory_service.h"
#include "services/player_lease_service.h"
#include "services/player_login_service.h"
#include "services/player_message_service.h"
#include "services/player_persistence_service.h"
#include "services/player_repository.h"
#include "services/player_runtime_service.h"
#include "services/player_session_service.h"

#include <spdlog/spdlog.h>

#include <sstream>

namespace
{
constexpr ipc::ServiceType kGameServiceType = 10;
constexpr std::uint32_t kPlayerLeaseTtlSeconds = 30;

bool AllMariaTargetsReachable(const some_server::storage::StorageSnapshot& snapshot)
{
    if (snapshot.maria.empty())
    {
        return false;
    }
    for (const auto& [_, status] : snapshot.maria)
    {
        if (!status.reachable)
        {
            return false;
        }
    }
    return true;
}

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

std::string JoinArguments(const CommandArguments& arguments, const std::size_t begin_index)
{
    std::ostringstream stream;
    for (std::size_t index = begin_index; index < arguments.size(); ++index)
    {
        if (index != begin_index)
        {
            stream << ' ';
        }
        stream << arguments[index];
    }
    return stream.str();
}
}

bool Application::OnConfigure()
{
    std::string error;
    if (!some_server::storage::ResolveStorageConfiguration(
            CommonConfig(), AppConfig().storage, mStorageConfiguration, error))
    {
        spdlog::error("Application::OnConfigure(storage) failed: {}", error);
        return false;
    }
    return true;
}

void Application::RegisterServices()
{
    auto storage_service = std::make_unique<some_server::storage::StorageService>(mStorageConfiguration);
    mStorageService = storage_service.get();
    AddService(std::move(storage_service));

    auto player_directory_service = std::make_unique<PlayerDirectoryService>(mStorageService, "directory");
    mPlayerDirectoryService = player_directory_service.get();
    AddService(std::move(player_directory_service));

    auto player_repository = std::make_unique<PlayerRepository>(mStorageService, "player");
    mPlayerRepository = player_repository.get();
    AddService(std::move(player_repository));

    auto player_lease_service =
        std::make_unique<PlayerLeaseService>(CommonConfig(), "default", kGameServiceType, AppConfig().instance_id, kPlayerLeaseTtlSeconds);
    mPlayerLeaseService = player_lease_service.get();
    AddService(std::move(player_lease_service));

    auto player_runtime_service = std::make_unique<PlayerRuntimeService>(mPlayerRepository);
    mPlayerRuntimeService = player_runtime_service.get();
    AddService(std::move(player_runtime_service));

    auto service = std::make_unique<GameIpcClientService>(AppConfig(), kGameServiceType);
    mIpcService = service.get();
    AddService(std::move(service));

    auto player_session_service =
        std::make_unique<PlayerSessionService>(
            mPlayerRepository,
            mPlayerLeaseService,
            mPlayerRuntimeService,
            mIpcService,
            300);
    mPlayerSessionService = player_session_service.get();
    AddService(std::move(player_session_service));

    auto player_persistence_service =
        std::make_unique<PlayerPersistenceService>(
            mStorageConfiguration,
            mPlayerLeaseService,
            mPlayerRepository,
            mPlayerSessionService);
    mPlayerPersistenceService = player_persistence_service.get();
    AddService(std::move(player_persistence_service));

    auto player_login_service =
        std::make_unique<PlayerLoginService>(mPlayerDirectoryService, mPlayerSessionService, mIpcService);
    mPlayerLoginService = player_login_service.get();
    AddService(std::move(player_login_service));

    auto player_message_service =
        std::make_unique<GamePlayerMessageService>(
            mPlayerSessionService,
            mPlayerLeaseService,
            mPlayerRepository,
            mPlayerRuntimeService,
            mIpcService);
    mPlayerMessageService = player_message_service.get();
    AddService(std::move(player_message_service));
}

void Application::RegisterRuntimeCommands()
{
    Runtime().RegisterCommand(
        "player_lease_status",
        "Show one player lease status: <player_id>",
        [this](const CommandArguments& arguments) {
            if (mPlayerLeaseService == nullptr)
            {
                spdlog::warn("player lease status: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: player_lease_status <player_id>");
                return CommandExecutionStatus::handled;
            }

            const auto player_id = static_cast<std::uint64_t>(std::stoull(arguments[0]));
            const auto local = mPlayerLeaseService->PlayerSnapshot(player_id);
            const auto owner = mPlayerLeaseService->Lookup(player_id);
            spdlog::info(
                "player lease status: player_id={} local_tracked={} local_owner={}:{} token={} remote_present={} remote_owner={}:{} remote_token={}",
                player_id,
                local.tracked,
                local.game_service_type,
                local.game_instance_id,
                local.tracked ? local.token : "none",
                owner.has_value(),
                owner.has_value() ? owner->game_service_type : 0,
                owner.has_value() ? owner->game_instance_id : 0,
                owner.has_value() ? owner->token : "none");
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_lease_service_status",
        "Show player lease service status",
        [this](const CommandArguments&) {
            if (mPlayerLeaseService == nullptr)
            {
                spdlog::warn("player lease service status: service not registered");
                return CommandExecutionStatus::handled;
            }
            const auto snapshot = mPlayerLeaseService->Snapshot();
            spdlog::info(
                "player lease service status: running={} tracked_player_count={} renew_success_count={} renew_failure_count={} local_loss_count={} release_success_count={} release_failure_count={} last_renew_ms={} last_error={}",
                snapshot.running,
                snapshot.tracked_player_count,
                snapshot.renew_success_count,
                snapshot.renew_failure_count,
                snapshot.local_loss_count,
                snapshot.release_success_count,
                snapshot.release_failure_count,
                snapshot.last_renew_ms,
                snapshot.last_error.empty() ? "none" : snapshot.last_error);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_persistence_status",
        "Show player persistence scheduler status",
        [this](const CommandArguments&) {
            if (mPlayerPersistenceService == nullptr)
            {
                spdlog::warn("player persistence status: service not registered");
                return CommandExecutionStatus::handled;
            }
            const auto snapshot = mPlayerPersistenceService->Snapshot();
            spdlog::info(
                "player persistence status: running={} scan_count={} tracked_player_count={} pending_initial_persist_count={} created_without_maria_count={} flush_attempt_count={} flush_success_count={} flush_failure_count={} last_scan_ms={} last_flush_player_id={} last_flush_ms={} last_error={}",
                snapshot.running,
                snapshot.scan_count,
                snapshot.tracked_player_count,
                snapshot.pending_initial_persist_count,
                snapshot.created_without_maria_count,
                snapshot.flush_attempt_count,
                snapshot.flush_success_count,
                snapshot.flush_failure_count,
                snapshot.last_scan_ms,
                snapshot.last_flush_player_id,
                snapshot.last_flush_ms,
                snapshot.last_error.empty() ? "none" : snapshot.last_error);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_persistence_player_status",
        "Show one player's persistence retry state: <player_id>",
        [this](const CommandArguments& arguments) {
            if (mPlayerPersistenceService == nullptr)
            {
                spdlog::warn("player persistence player status: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: player_persistence_player_status <player_id>");
                return CommandExecutionStatus::handled;
            }

            const auto player_id = static_cast<std::uint64_t>(std::stoull(arguments[0]));
            const auto snapshot = mPlayerPersistenceService->PlayerSnapshot(player_id);
            spdlog::info(
                "player persistence player status: player_id={} tracked={} repository_present={} loaded={} dirty={} pending_initial_persist={} created_without_maria={} flush_count={} last_dirty_ms={} last_flush_ms={} consecutive_failure_count={} next_retry_ms={} last_failure_ms={} last_error={}",
                player_id,
                snapshot.tracked,
                snapshot.repository_present,
                snapshot.loaded,
                snapshot.dirty,
                snapshot.pending_initial_persist,
                snapshot.created_without_maria,
                snapshot.flush_count,
                snapshot.last_dirty_ms,
                snapshot.last_flush_ms,
                snapshot.consecutive_failure_count,
                snapshot.next_retry_ms,
                snapshot.last_failure_ms,
                snapshot.last_error.empty() ? "none" : snapshot.last_error);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_persistence_flush_due",
        "Run one immediate player persistence scan",
        [this](const CommandArguments&) {
            if (mPlayerPersistenceService == nullptr)
            {
                spdlog::warn("player persistence flush due: service not registered");
                return CommandExecutionStatus::handled;
            }
            const auto flushed = mPlayerPersistenceService->FlushDuePlayersOnce();
            spdlog::info("player persistence flush due: flushed={}", flushed);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_persistence_recover_flush",
        "Probe storage, clear persistence backoff if Maria is reachable, then flush due players",
        [this](const CommandArguments&) {
            if (mStorageService == nullptr || mPlayerPersistenceService == nullptr)
            {
                spdlog::warn("player persistence recover flush: services not registered");
                return CommandExecutionStatus::handled;
            }

            const auto snapshot = mStorageService->ProbeNow();
            const bool maria_reachable = AllMariaTargetsReachable(snapshot);
            const auto reset = maria_reachable ? mPlayerPersistenceService->ResetRetryStates() : std::size_t{0};
            const auto flushed = maria_reachable ? mPlayerPersistenceService->FlushDuePlayersOnce() : std::size_t{0};
            spdlog::info(
                "player persistence recover flush: maria_reachable={} reset_retry_states={} flushed={}",
                maria_reachable,
                reset,
                flushed);
            if (!maria_reachable)
            {
                for (const auto& [name, status] : snapshot.maria)
                {
                    spdlog::info(
                        "player persistence recover flush maria: name={} reachable={} error={}",
                        name,
                        status.reachable,
                        status.error.empty() ? "none" : status.error);
                }
            }
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_runtime_status",
        "Show one player runtime instance status: <player_id>",
        [this](const CommandArguments& arguments) {
            if (mPlayerRuntimeService == nullptr)
            {
                spdlog::warn("player runtime status: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: player_runtime_status <player_id>");
                return CommandExecutionStatus::handled;
            }

            const auto player_id = static_cast<std::uint64_t>(std::stoull(arguments[0]));
            const auto runtime = mPlayerRuntimeService->Snapshot(player_id);
            spdlog::info(
                "player runtime status: player_id={} present={} command_count={}",
                player_id,
                runtime.present,
                runtime.command_count);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_push_profile",
        "Push one typed profile message to the player's active gate session: <player_id>",
        [this](const CommandArguments& arguments) {
            if (mPlayerMessageService == nullptr)
            {
                spdlog::warn("player push profile: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: player_push_profile <player_id>");
                return CommandExecutionStatus::handled;
            }

            const auto player_id = static_cast<std::uint64_t>(std::stoull(arguments[0]));
            const auto result = mPlayerMessageService->PushProfileToPlayer(player_id);
            if (!result.ok)
            {
                spdlog::warn("player push profile failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("player push profile: player_id={}", player_id);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_push",
        "Push one message to the player's active gate session: <player_id> <message_id> [payload]",
        [this](const CommandArguments& arguments) {
            if (mPlayerMessageService == nullptr)
            {
                spdlog::warn("player push: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() < 2 || arguments.size() > 3)
            {
                spdlog::warn("usage: player_push <player_id> <message_id> [payload]");
                return CommandExecutionStatus::handled;
            }

            const auto player_id = static_cast<std::uint64_t>(std::stoull(arguments[0]));
            const auto message_id = static_cast<std::uint32_t>(std::stoul(arguments[1]));
            const std::string payload = arguments.size() == 3 ? arguments[2] : "server-push";
            const auto result = mPlayerMessageService->PushToPlayer(player_id, message_id, payload);
            if (!result.ok)
            {
                spdlog::warn("player push failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("player push: player_id={} message_id={} payload={}", player_id, message_id, payload);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "status",
        "Show game runtime status",
        [](const CommandArguments&) {
            spdlog::info("game status: {}", "running");
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_directory_resolve",
        "Resolve or create one player id for <platform> <account_id> <area_id>",
        [this](const CommandArguments& arguments) {
            if (mPlayerDirectoryService == nullptr)
            {
                spdlog::warn("player directory: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 3)
            {
                spdlog::warn("usage: player_directory_resolve <platform> <account_id> <area_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint32_t area_id = 0;
            try
            {
                area_id = static_cast<std::uint32_t>(std::stoul(arguments[2]));
            }
            catch (const std::exception&)
            {
                spdlog::warn("player directory: area_id must be an unsigned integer");
                return CommandExecutionStatus::handled;
            }

            const auto result = mPlayerDirectoryService->ResolveOrCreate(arguments[0], arguments[1], area_id);
            spdlog::info(
                "player directory resolve: platform={} account_id={} area_id={} player_id={} created={}",
                arguments[0],
                arguments[1],
                area_id,
                result.player_id,
                result.created);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_directory_lookup",
        "Lookup one player id for <platform> <account_id> <area_id> without creating",
        [this](const CommandArguments& arguments) {
            if (mPlayerDirectoryService == nullptr)
            {
                spdlog::warn("player directory: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 3)
            {
                spdlog::warn("usage: player_directory_lookup <platform> <account_id> <area_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint32_t area_id = 0;
            try
            {
                area_id = static_cast<std::uint32_t>(std::stoul(arguments[2]));
            }
            catch (const std::exception&)
            {
                spdlog::warn("player directory: area_id must be an unsigned integer");
                return CommandExecutionStatus::handled;
            }

            const auto result = mPlayerDirectoryService->Lookup(arguments[0], arguments[1], area_id);
            if (!result.has_value())
            {
                spdlog::info(
                    "player directory lookup: platform={} account_id={} area_id={} present=false",
                    arguments[0],
                    arguments[1],
                    area_id);
                return CommandExecutionStatus::handled;
            }

            spdlog::info(
                "player directory lookup: platform={} account_id={} area_id={} present=true player_id={}",
                arguments[0],
                arguments[1],
                area_id,
                *result);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_directory_counter",
        "Show persisted player directory next-player-id counter",
        [this](const CommandArguments& arguments) {
            if (mPlayerDirectoryService == nullptr)
            {
                spdlog::warn("player directory: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (!arguments.empty())
            {
                spdlog::warn("usage: player_directory_counter");
                return CommandExecutionStatus::handled;
            }

            const auto result = mPlayerDirectoryService->NextPlayerIdCounter();
            if (!result.has_value())
            {
                spdlog::info("player directory counter: present=false");
                return CommandExecutionStatus::handled;
            }

            spdlog::info("player directory counter: present=true next_player_id={}", *result);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_status",
        "Show one player session or repository status",
        [this](const CommandArguments& arguments) {
            if (mPlayerSessionService == nullptr || mPlayerRepository == nullptr)
            {
                spdlog::warn("player status: services not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: player_status <player_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t player_id = 0;
            try
            {
                player_id = std::stoull(arguments[0]);
            }
            catch (const std::exception&)
            {
                spdlog::warn("player status: player_id must be an unsigned integer");
                return CommandExecutionStatus::handled;
            }

            const auto session = mPlayerSessionService->Snapshot(player_id);
            const auto repository = mPlayerRepository->Snapshot(player_id);
            spdlog::info(
                "player status: player_id={} session_present={} state={} active_gate={}:{} gate_session_id={} activate_count={} last_state_change_ms={} detach_deadline_ms={} repository_present={} loaded={} dirty={} pending_initial_persist={} created_without_maria={} load_count={} flush_count={} last_load_ms={} last_dirty_ms={} last_flush_ms={}",
                player_id,
                session.has_value(),
                session.has_value() ? ToString(session->state) : "none",
                session.has_value() ? session->gate_service_type : 0,
                session.has_value() ? session->gate_instance_id : 0,
                session.has_value() ? session->gate_session_id : 0,
                session.has_value() ? session->activate_count : 0,
                session.has_value() ? session->last_state_change_ms : 0,
                session.has_value() ? session->detach_deadline_ms : 0,
                repository.has_value(),
                repository.has_value() ? repository->loaded : false,
                repository.has_value() ? repository->dirty : false,
                repository.has_value() ? repository->pending_initial_persist : false,
                repository.has_value() ? repository->created_without_maria : false,
                repository.has_value() ? repository->load_count : 0,
                repository.has_value() ? repository->flush_count : 0,
                repository.has_value() ? repository->last_load_ms : 0,
                repository.has_value() ? repository->last_dirty_ms : 0,
                repository.has_value() ? repository->last_flush_ms : 0);
            if (repository.has_value())
            {
                spdlog::info(
                    "player data: player_id={} name={} created_at_ms={} last_login_at_ms={} level={} experience={} login_count={}",
                    repository->data.base().player_id(),
                    repository->data.base().display_name(),
                    repository->data.base().created_at_ms(),
                    repository->data.base().last_login_at_ms(),
                    repository->data.core().level(),
                    repository->data.core().experience(),
                    repository->data.core().login_count());
            }
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_activate",
        "Activate one player on this game process: <player_id> [gate_service_type gate_instance_id gate_session_id]",
        [this](const CommandArguments& arguments) {
            if (mPlayerSessionService == nullptr)
            {
                spdlog::warn("player activate: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1 && arguments.size() != 4)
            {
                spdlog::warn(
                    "usage: player_activate <player_id> [gate_service_type gate_instance_id gate_session_id]");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t player_id = 0;
            std::uint32_t gate_service_type = 0;
            std::uint32_t gate_instance_id = 0;
            std::uint64_t gate_session_id = 0;
            try
            {
                player_id = std::stoull(arguments[0]);
                if (arguments.size() == 4)
                {
                    gate_service_type = static_cast<std::uint32_t>(std::stoul(arguments[1]));
                    gate_instance_id = static_cast<std::uint32_t>(std::stoul(arguments[2]));
                    gate_session_id = std::stoull(arguments[3]);
                }
            }
            catch (const std::exception&)
            {
                spdlog::warn("player activate: arguments must be unsigned integers");
                return CommandExecutionStatus::handled;
            }

            const auto result =
                mPlayerSessionService->ActivatePlayer(player_id, gate_service_type, gate_instance_id, gate_session_id);
            if (!result.ok)
            {
                spdlog::warn("player activate failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info(
                "player activate: player_id={} gate={}:{} gate_session_id={} result={}",
                player_id,
                gate_service_type,
                gate_instance_id,
                gate_session_id,
                result.message);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_detach",
        "Move one player to detached state: <player_id>",
        [this](const CommandArguments& arguments) {
            if (mPlayerSessionService == nullptr)
            {
                spdlog::warn("player detach: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: player_detach <player_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t player_id = 0;
            try
            {
                player_id = std::stoull(arguments[0]);
            }
            catch (const std::exception&)
            {
                spdlog::warn("player detach: player_id must be an unsigned integer");
                return CommandExecutionStatus::handled;
            }

            const auto result = mPlayerSessionService->DetachPlayer(player_id);
            if (!result.ok)
            {
                spdlog::warn("player detach failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("player detach: player_id={} result={}", player_id, result.message);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_reconcile_lease_loss",
        "Reconcile players that no longer hold a local lease",
        [this](const CommandArguments&) {
            if (mPlayerSessionService == nullptr)
            {
                spdlog::warn("player reconcile lease loss: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto reconciled = mPlayerSessionService->ReconcileLeaseLosses();
            spdlog::info("player reconcile lease loss: reconciled={}", reconciled);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_release",
        "Release one player from this game process: <player_id>",
        [this](const CommandArguments& arguments) {
            if (mPlayerSessionService == nullptr)
            {
                spdlog::warn("player release: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: player_release <player_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t player_id = 0;
            try
            {
                player_id = std::stoull(arguments[0]);
            }
            catch (const std::exception&)
            {
                spdlog::warn("player release: player_id must be an unsigned integer");
                return CommandExecutionStatus::handled;
            }

            const auto result = mPlayerSessionService->ReleasePlayer(player_id);
            if (!result.ok)
            {
                const auto session = mPlayerSessionService->Snapshot(player_id);
                const auto repository = mPlayerRepository != nullptr
                    ? mPlayerRepository->Snapshot(player_id)
                    : std::nullopt;
                spdlog::warn(
                    "player release failed: player_id={} error={} session_present={} state={} repository_present={} loaded={} dirty={} pending_initial_persist={} created_without_maria={} flush_count={} last_dirty_ms={} last_flush_ms={}",
                    player_id,
                    result.message,
                    session.has_value(),
                    session.has_value() ? ToString(session->state) : "none",
                    repository.has_value(),
                    repository.has_value() ? repository->loaded : false,
                    repository.has_value() ? repository->dirty : false,
                    repository.has_value() ? repository->pending_initial_persist : false,
                    repository.has_value() ? repository->created_without_maria : false,
                    repository.has_value() ? repository->flush_count : 0,
                    repository.has_value() ? repository->last_dirty_ms : 0,
                    repository.has_value() ? repository->last_flush_ms : 0);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("player release: player_id={} result={}", player_id, result.message);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_touch_login",
        "Touch one player's login counters: <player_id>",
        [this](const CommandArguments& arguments) {
            if (mPlayerRepository == nullptr)
            {
                spdlog::warn("player touch login: repository not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: player_touch_login <player_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t player_id = 0;
            try
            {
                player_id = std::stoull(arguments[0]);
            }
            catch (const std::exception&)
            {
                spdlog::warn("player touch login: player_id must be an unsigned integer");
                return CommandExecutionStatus::handled;
            }

            const auto result = mPlayerRepository->TouchLogin(player_id);
            if (!result.ok)
            {
                spdlog::warn("player touch login failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("player touch login: player_id={}", player_id);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_set_name",
        "Set one player's display name: <player_id> <display_name...>",
        [this](const CommandArguments& arguments) {
            if (mPlayerRepository == nullptr)
            {
                spdlog::warn("player set name: repository not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() < 2)
            {
                spdlog::warn("usage: player_set_name <player_id> <display_name...>");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t player_id = 0;
            try
            {
                player_id = std::stoull(arguments[0]);
            }
            catch (const std::exception&)
            {
                spdlog::warn("player set name: player_id must be an unsigned integer");
                return CommandExecutionStatus::handled;
            }

            const std::string display_name = JoinArguments(arguments, 1);
            const auto result = mPlayerRepository->SetDisplayName(player_id, display_name);
            if (!result.ok)
            {
                spdlog::warn("player set name failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("player set name: player_id={} display_name={}", player_id, display_name);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_flush",
        "Flush one loaded player into storage: <player_id>",
        [this](const CommandArguments& arguments) {
            if (mPlayerRepository == nullptr)
            {
                spdlog::warn("player flush: repository not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: player_flush <player_id>");
                return CommandExecutionStatus::handled;
            }

            std::uint64_t player_id = 0;
            try
            {
                player_id = std::stoull(arguments[0]);
            }
            catch (const std::exception&)
            {
                spdlog::warn("player flush: player_id must be an unsigned integer");
                return CommandExecutionStatus::handled;
            }

            const auto result = mPlayerRepository->FlushPlayer(player_id);
            if (!result.ok)
            {
                spdlog::warn("player flush failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("player flush: player_id={}", player_id);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "player_expire_detached",
        "Release all detached players past their deadline",
        [this](const CommandArguments&) {
            if (mPlayerSessionService == nullptr)
            {
                spdlog::warn("player expire detached: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto released = mPlayerSessionService->ExpireDetachedPlayers();
            spdlog::info("player expire detached: released={}", released);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "storage_status",
        "Show resolved storage datasets and reachability",
        [this](const CommandArguments&) {
            if (mStorageService == nullptr)
            {
                spdlog::warn("storage status: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto snapshot = mStorageService->Snapshot();
            spdlog::info(
                "storage status: datasets={} redis_targets={} maria_targets={}",
                snapshot.dataset_count,
                snapshot.redis.size(),
                snapshot.maria.size());
            for (const auto& [name, status] : snapshot.redis)
            {
                spdlog::info(
                    "storage redis: name={} target={} reachable={} error={}",
                    name,
                    status.target.empty() ? "none" : status.target,
                    status.reachable,
                    status.error.empty() ? "none" : status.error);
            }
            for (const auto& [name, status] : snapshot.maria)
            {
                spdlog::info(
                    "storage maria: name={} target={} reachable={} error={}",
                    name,
                    status.target.empty() ? "none" : status.target,
                    status.reachable,
                    status.error.empty() ? "none" : status.error);
            }
            for (const auto& [dataset_name, dataset] : mStorageConfiguration.Datasets())
            {
                spdlog::info(
                    "storage dataset: name={} redis={} maria={} redis_prefix={} full_redis_prefix={} table_prefix={} alive_time_seconds={} landing_time_seconds={} landing_min_time_seconds={}",
                    dataset_name,
                    dataset.redis_name,
                    dataset.maria_name,
                    dataset.redis_prefix,
                    dataset.full_redis_prefix,
                    dataset.table_prefix,
                    dataset.timing.alive_time_seconds,
                    dataset.timing.landing_time_seconds,
                    dataset.timing.landing_min_time_seconds);
            }
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "storage_probe",
        "Probe resolved Redis and Maria targets again",
        [this](const CommandArguments&) {
            if (mStorageService == nullptr)
            {
                spdlog::warn("storage probe: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto snapshot = mStorageService->ProbeNow();
            spdlog::info(
                "storage probe: datasets={} redis_targets={} maria_targets={}",
                snapshot.dataset_count,
                snapshot.redis.size(),
                snapshot.maria.size());
            for (const auto& [name, status] : snapshot.redis)
            {
                spdlog::info(
                    "storage redis probe: name={} target={} reachable={} error={}",
                    name,
                    status.target.empty() ? "none" : status.target,
                    status.reachable,
                    status.error.empty() ? "none" : status.error);
            }
            for (const auto& [name, status] : snapshot.maria)
            {
                spdlog::info(
                    "storage maria probe: name={} target={} reachable={} error={}",
                    name,
                    status.target.empty() ? "none" : status.target,
                    status.reachable,
                    status.error.empty() ? "none" : status.error);
            }
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "storage_disable_maria",
        "Disable one Maria target locally: <name>",
        [this](const CommandArguments& arguments) {
            if (mStorageService == nullptr)
            {
                spdlog::warn("storage disable maria: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: storage_disable_maria <name>");
                return CommandExecutionStatus::handled;
            }

            if (!mStorageService->DisableMariaTarget(arguments[0]))
            {
                spdlog::warn("storage disable maria failed: unknown target {}", arguments[0]);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("storage disable maria: name={}", arguments[0]);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "storage_enable_maria",
        "Enable one Maria target locally: <name>",
        [this](const CommandArguments& arguments) {
            if (mStorageService == nullptr)
            {
                spdlog::warn("storage enable maria: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: storage_enable_maria <name>");
                return CommandExecutionStatus::handled;
            }

            if (!mStorageService->EnableMariaTarget(arguments[0]))
            {
                spdlog::warn("storage enable maria failed: target was not disabled {}", arguments[0]);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("storage enable maria: name={}", arguments[0]);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "storage_dataset_init",
        "Create the generic Maria entries table for a dataset without running schema migration",
        [this](const CommandArguments& arguments) {
            if (mStorageService == nullptr)
            {
                spdlog::warn("storage dataset init: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: storage_dataset_init <dataset>");
                return CommandExecutionStatus::handled;
            }

            const auto result = mStorageService->EnsureMariaEntriesTable(arguments[0]);
            if (!result.ok)
            {
                spdlog::warn("storage dataset init failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info(
                "storage dataset init: dataset={} table={}",
                arguments[0],
                mStorageService->BuildMariaEntriesTableName(arguments[0]));
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "storage_dataset_migrate_binary",
        "Migrate one generic Maria entries table to LONGBLOB: <dataset>",
        [this](const CommandArguments& arguments) {
            if (mStorageService == nullptr)
            {
                spdlog::warn("storage dataset migrate binary: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 1)
            {
                spdlog::warn("usage: storage_dataset_migrate_binary <dataset>");
                return CommandExecutionStatus::handled;
            }

            const auto result = mStorageService->MigrateMariaEntriesTableToBinary(arguments[0]);
            if (!result.ok)
            {
                spdlog::warn("storage dataset migrate binary failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info(
                "storage dataset migrate binary: dataset={} table={}",
                arguments[0],
                mStorageService->BuildMariaEntriesTableName(arguments[0]));
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "storage_dataset_put",
        "Write one generic dataset entry to Maria and Redis",
        [this](const CommandArguments& arguments) {
            if (mStorageService == nullptr)
            {
                spdlog::warn("storage dataset put: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() < 3)
            {
                spdlog::warn("usage: storage_dataset_put <dataset> <entry_key> <value...>");
                return CommandExecutionStatus::handled;
            }

            const std::string value = JoinArguments(arguments, 2);
            const auto result = mStorageService->DatasetPut(arguments[0], arguments[1], value);
            if (!result.ok)
            {
                spdlog::warn("storage dataset put failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("storage dataset put: dataset={} entry_key={} result={}", arguments[0], arguments[1], result.message);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "storage_dataset_get",
        "Read one generic dataset entry with Redis read-through",
        [this](const CommandArguments& arguments) {
            if (mStorageService == nullptr)
            {
                spdlog::warn("storage dataset get: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 2)
            {
                spdlog::warn("usage: storage_dataset_get <dataset> <entry_key>");
                return CommandExecutionStatus::handled;
            }

            std::string value;
            const auto result = mStorageService->DatasetGet(arguments[0], arguments[1], value);
            if (!result.ok)
            {
                spdlog::warn("storage dataset get failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info(
                "storage dataset get: dataset={} entry_key={} source={} value={}",
                arguments[0],
                arguments[1],
                result.message,
                value);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "storage_dataset_del",
        "Delete one generic dataset entry from Maria and Redis",
        [this](const CommandArguments& arguments) {
            if (mStorageService == nullptr)
            {
                spdlog::warn("storage dataset del: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 2)
            {
                spdlog::warn("usage: storage_dataset_del <dataset> <entry_key>");
                return CommandExecutionStatus::handled;
            }

            const auto result = mStorageService->DatasetDelete(arguments[0], arguments[1]);
            if (!result.ok)
            {
                spdlog::warn("storage dataset del failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("storage dataset del: dataset={} entry_key={} result={}", arguments[0], arguments[1], result.message);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "storage_redis_set",
        "Set one Redis string value through a dataset binding",
        [this](const CommandArguments& arguments) {
            if (mStorageService == nullptr)
            {
                spdlog::warn("storage redis set: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() < 3)
            {
                spdlog::warn("usage: storage_redis_set <dataset> <key_suffix> <value...>");
                return CommandExecutionStatus::handled;
            }

            const std::string value = JoinArguments(arguments, 2);
            const auto result = mStorageService->RedisSet(arguments[0], arguments[1], value);
            if (!result.ok)
            {
                spdlog::warn("storage redis set failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info(
                "storage redis set: dataset={} key={} result={}",
                arguments[0],
                mStorageService->BuildRedisKey(arguments[0], arguments[1]),
                result.message);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "storage_redis_get",
        "Get one Redis string value through a dataset binding",
        [this](const CommandArguments& arguments) {
            if (mStorageService == nullptr)
            {
                spdlog::warn("storage redis get: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 2)
            {
                spdlog::warn("usage: storage_redis_get <dataset> <key_suffix>");
                return CommandExecutionStatus::handled;
            }

            std::string value;
            const auto result = mStorageService->RedisGet(arguments[0], arguments[1], value);
            if (!result.ok)
            {
                spdlog::warn("storage redis get failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info(
                "storage redis get: dataset={} key={} value={}",
                arguments[0],
                mStorageService->BuildRedisKey(arguments[0], arguments[1]),
                value);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "storage_redis_del",
        "Delete one Redis key through a dataset binding",
        [this](const CommandArguments& arguments) {
            if (mStorageService == nullptr)
            {
                spdlog::warn("storage redis del: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() != 2)
            {
                spdlog::warn("usage: storage_redis_del <dataset> <key_suffix>");
                return CommandExecutionStatus::handled;
            }

            const auto result = mStorageService->RedisDelete(arguments[0], arguments[1]);
            if (!result.ok)
            {
                spdlog::warn("storage redis del failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info(
                "storage redis del: dataset={} key={} result={}",
                arguments[0],
                mStorageService->BuildRedisKey(arguments[0], arguments[1]),
                result.message);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "storage_maria_exec",
        "Execute one Maria SQL statement through a dataset binding",
        [this](const CommandArguments& arguments) {
            if (mStorageService == nullptr)
            {
                spdlog::warn("storage maria exec: service not registered");
                return CommandExecutionStatus::handled;
            }
            if (arguments.size() < 2)
            {
                spdlog::warn("usage: storage_maria_exec <dataset> <sql...>");
                return CommandExecutionStatus::handled;
            }

            std::string summary;
            const std::string sql = JoinArguments(arguments, 1);
            const auto result = mStorageService->MariaExecute(arguments[0], sql, summary);
            if (!result.ok)
            {
                spdlog::warn("storage maria exec failed: {}", result.message);
                return CommandExecutionStatus::handled;
            }
            spdlog::info("storage maria exec: dataset={} sql={} summary={}", arguments[0], sql, summary);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
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
                "game ipc status: service_type={} instance_id={} transport_ready={} registered={} ipc_ready={} membership_degraded={} keepalive_running={} watch_running={} members={} relay_member_visible={} healthy_relay_link={} auto_connect_targets={} auto_connect_success_count={} auto_connect_failure_count={} last_auto_connect_target={}:{} last_auto_connect_failure_target={}:{} last_auto_connect_failure_reason={} process_dispatch_count={} last_process_payload_type={} player_dispatch_count={} last_player_id={} last_player_payload_type={} local_service_dispatch_count={} last_payload_type={} last_error={}",
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
                status.player_dispatch_count,
                status.last_player_id,
                status.last_player_payload_type.empty() ? "none" : status.last_player_payload_type,
                status.local_service_dispatch_count,
                status.last_payload_type.empty() ? "none" : status.last_payload_type,
                status.last_error.empty() ? "none" : status.last_error);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
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

    Runtime().RegisterCommand(
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

    Runtime().RegisterCommand(
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

    Runtime().RegisterCommand(
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

    Runtime().RegisterCommand(
        "ipc_topology",
        "Show game IPC topology and auto-connect state",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc topology: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto status = mIpcService->Snapshot();
            const auto links = mIpcService->HealthyLinks();
            spdlog::info(
                "game ipc topology: relay_member_visible={} healthy_relay_link={} healthy_links={} auto_connect_targets={} auto_connect_success_count={} auto_connect_failure_count={} last_auto_connect_target={}:{} last_auto_connect_failure_target={}:{} last_auto_connect_failure_reason={}",
                status.relay_member_visible,
                status.healthy_relay_link,
                links.size(),
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
                status.last_auto_connect_failure_reason.empty() ? "none" : status.last_auto_connect_failure_reason);
            for (const auto& link : links)
            {
                spdlog::info(
                    "game ipc topology link: service_type={} instance_id={} incarnation={} role={}",
                    link.process_id.service_type,
                    link.process_id.instance_id,
                    link.incarnation_id,
                    link.process_id.service_type == kGameServiceType ? "game" : "relay");
            }
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
        "ipc_metrics",
        "Show game IPC runtime metrics",
        [this](const CommandArguments&) {
            if (mIpcService == nullptr)
            {
                spdlog::warn("game ipc metrics: service not registered");
                return CommandExecutionStatus::handled;
            }

            const auto status = mIpcService->Snapshot();
            spdlog::info(
                "game ipc metrics: keepalive_failure_count={} discovery_recovery_success_count={} discovery_recovery_failure_count={} send_reject_count={} last_send_reject_reason={} watch_restart_count={} watch_start_failure_count={} watch_stream_closed_count={} snapshot_refresh_failure_count={}",
                status.keepalive_failure_count,
                status.discovery_recovery_success_count,
                status.discovery_recovery_failure_count,
                status.send_reject_count,
                status.last_send_reject_reason.empty() ? "none" : status.last_send_reject_reason,
                status.discovery_runtime.watch_restart_count,
                status.discovery_runtime.watch_start_failure_count,
                status.discovery_runtime.watch_stream_closed_count,
                status.discovery_runtime.snapshot_refresh_failure_count);
            return CommandExecutionStatus::handled;
        });

    Runtime().RegisterCommand(
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

    Runtime().RegisterCommand(
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

    Runtime().RegisterCommand(
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

    Runtime().RegisterCommand(
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

    Runtime().RegisterCommand(
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

    Runtime().RegisterCommand(
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

    Runtime().RegisterCommand(
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

    Runtime().RegisterCommand(
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
}

LifecycleTask Application::OnLoad()
{
    spdlog::info("Application::Configure(listen={}:{})",
                 AppConfig().listen.host,
                 AppConfig().listen.port);
    spdlog::info("Application::Configure(storage_datasets={})", mStorageConfiguration.Size());
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
