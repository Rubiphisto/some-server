#include "player_runtime_service.h"

ipc::Result PlayerInstance::HandleEcho(
    const pb::PlayerEchoRequest& request,
    pb::PlayerEchoResponse& response)
{
    if (mRepository == nullptr)
    {
        return ipc::Result::Failure("player repository is not registered");
    }

    const auto repository = mRepository->Snapshot(mPlayerId);
    if (!repository.has_value() || !repository->loaded)
    {
        return ipc::Result::Failure("player repository is not loaded");
    }

    ++mCommandCount;
    response.set_player_id(mPlayerId);
    response.set_text(request.text());
    response.set_display_name(repository->data.base().display_name());
    response.set_login_count(repository->data.core().login_count());
    return ipc::Result::Success();
}

ipc::Result PlayerInstance::HandleRename(
    const pb::RenamePlayerRequest& request,
    pb::RenamePlayerResponse& response)
{
    if (mRepository == nullptr)
    {
        return ipc::Result::Failure("player repository is not registered");
    }
    if (request.display_name().empty())
    {
        return ipc::Result::Failure("display_name must not be empty");
    }

    const auto set_name = mRepository->SetDisplayName(mPlayerId, request.display_name());
    if (!set_name.ok)
    {
        return set_name;
    }

    ++mCommandCount;
    response.set_player_id(mPlayerId);
    response.set_display_name(request.display_name());
    return ipc::Result::Success();
}

std::optional<std::string> PlayerInstance::BuildProfilePushPayload() const
{
    if (mRepository == nullptr)
    {
        return std::nullopt;
    }

    const auto repository = mRepository->Snapshot(mPlayerId);
    if (!repository.has_value() || !repository->loaded)
    {
        return std::nullopt;
    }

    pb::PlayerProfilePush push;
    push.set_player_id(mPlayerId);
    push.set_display_name(repository->data.base().display_name());
    push.set_level(repository->data.core().level());
    push.set_login_count(repository->data.core().login_count());

    std::string payload;
    if (!push.SerializeToString(&payload))
    {
        return std::nullopt;
    }
    return payload;
}

ipc::Result PlayerRuntimeService::EnsurePlayer(const std::uint64_t player_id)
{
    if (mRepository == nullptr)
    {
        return ipc::Result::Failure("player repository is not registered");
    }

    const auto repository = mRepository->Snapshot(player_id);
    if (!repository.has_value() || !repository->loaded)
    {
        return ipc::Result::Failure("player repository is not loaded");
    }

    std::scoped_lock lock(mMutex);
    if (!mPlayers.contains(player_id))
    {
        mPlayers.emplace(player_id, std::make_unique<PlayerInstance>(player_id, mRepository));
    }
    return ipc::Result::Success();
}

ipc::Result PlayerRuntimeService::RemovePlayer(const std::uint64_t player_id)
{
    std::scoped_lock lock(mMutex);
    if (mPlayers.erase(player_id) == 0)
    {
        return ipc::Result::Failure("player runtime is not tracked");
    }
    return ipc::Result::Success();
}

ipc::Result PlayerRuntimeService::HandleEcho(
    const std::uint64_t player_id,
    const pb::PlayerEchoRequest& request,
    pb::PlayerEchoResponse& response)
{
    std::scoped_lock lock(mMutex);
    const auto it = mPlayers.find(player_id);
    if (it == mPlayers.end())
    {
        return ipc::Result::Failure("player runtime is not tracked");
    }
    return it->second->HandleEcho(request, response);
}

ipc::Result PlayerRuntimeService::HandleRename(
    const std::uint64_t player_id,
    const pb::RenamePlayerRequest& request,
    pb::RenamePlayerResponse& response)
{
    std::scoped_lock lock(mMutex);
    const auto it = mPlayers.find(player_id);
    if (it == mPlayers.end())
    {
        return ipc::Result::Failure("player runtime is not tracked");
    }
    return it->second->HandleRename(request, response);
}

std::optional<std::string> PlayerRuntimeService::BuildProfilePushPayload(const std::uint64_t player_id) const
{
    std::scoped_lock lock(mMutex);
    const auto it = mPlayers.find(player_id);
    if (it == mPlayers.end())
    {
        return std::nullopt;
    }
    return it->second->BuildProfilePushPayload();
}

PlayerRuntimeSnapshot PlayerRuntimeService::Snapshot(const std::uint64_t player_id) const
{
    std::scoped_lock lock(mMutex);
    const auto it = mPlayers.find(player_id);
    if (it == mPlayers.end())
    {
        return PlayerRuntimeSnapshot{};
    }

    PlayerRuntimeSnapshot snapshot;
    snapshot.present = true;
    snapshot.player_id = it->second->PlayerId();
    snapshot.command_count = it->second->CommandCount();
    return snapshot;
}
