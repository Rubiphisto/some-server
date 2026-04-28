#include "player_receiver_host.h"

#include <mutex>

bool PlayerReceiverHost::CanHandle(const ipc::ReceiverType type) const
{
    return type == ipc::ReceiverType::player;
}

ipc::DispatchResult PlayerReceiverHost::Dispatch(const ipc::ReceiverAddress& target, const ipc::Envelope& envelope)
{
    if (target.type != ipc::ReceiverType::player)
    {
        return ipc::DispatchResult::Failure("unsupported receiver type");
    }

    const std::uint64_t player_id = target.key_hi;
    {
        std::shared_lock lock(mMutex);
        if (!mPlayers.contains(player_id))
        {
            return ipc::DispatchResult::Failure("player receiver is not bound locally");
        }
        mLastPayloadType = envelope.payload_type_url;
    }

    ++mDispatchCount;
    mLastPlayerId.store(player_id);
    return ipc::DispatchResult::Success();
}

bool PlayerReceiverHost::Bind(const std::uint64_t player_id)
{
    std::scoped_lock lock(mMutex);
    return mPlayers.insert(player_id).second;
}

bool PlayerReceiverHost::Unbind(const std::uint64_t player_id)
{
    std::scoped_lock lock(mMutex);
    return mPlayers.erase(player_id) > 0;
}

bool PlayerReceiverHost::IsBound(const std::uint64_t player_id) const
{
    std::shared_lock lock(mMutex);
    return mPlayers.contains(player_id);
}

std::vector<std::uint64_t> PlayerReceiverHost::BoundPlayers() const
{
    std::shared_lock lock(mMutex);
    std::vector<std::uint64_t> players;
    players.reserve(mPlayers.size());
    for (const auto player_id : mPlayers)
    {
        players.push_back(player_id);
    }
    return players;
}

std::uint64_t PlayerReceiverHost::DispatchCount() const
{
    return mDispatchCount.load();
}

std::uint64_t PlayerReceiverHost::LastPlayerId() const
{
    return mLastPlayerId.load();
}

const std::string& PlayerReceiverHost::LastPayloadType() const
{
    return mLastPayloadType;
}
