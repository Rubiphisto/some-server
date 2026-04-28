#pragma once

#include "../../framework/ipc/receiver/receiver_host.h"

#include <atomic>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <vector>
#include <unordered_set>

class PlayerReceiverHost final : public ipc::IReceiverHost
{
public:
    bool CanHandle(ipc::ReceiverType type) const override;
    ipc::DispatchResult Dispatch(const ipc::ReceiverAddress& target, const ipc::Envelope& envelope) override;

    bool Bind(std::uint64_t player_id);
    bool Unbind(std::uint64_t player_id);
    bool IsBound(std::uint64_t player_id) const;
    std::vector<std::uint64_t> BoundPlayers() const;

    std::uint64_t DispatchCount() const;
    std::uint64_t LastPlayerId() const;
    const std::string& LastPayloadType() const;

private:
    mutable std::shared_mutex mMutex;
    std::unordered_set<std::uint64_t> mPlayers;
    std::atomic<std::uint64_t> mDispatchCount = 0;
    std::atomic<std::uint64_t> mLastPlayerId = 0;
    std::string mLastPayloadType;
};
