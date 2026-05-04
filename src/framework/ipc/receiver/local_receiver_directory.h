#pragma once

#include "receiver_directory.h"

#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace ipc
{
// Simple in-process receiver directory used by first-phase application services.
class LocalReceiverDirectory final : public IReceiverDirectory
{
public:
    // Resolves a receiver using only locally known ownership bindings.
    ReceiverLocation Resolve(const ReceiverAddress& receiver) const override;
    // Creates a new local binding for receiver -> owner.
    Result Bind(const ReceiverAddress& receiver, const ProcessRef& owner) override;
    // Replaces the current owner binding when old_owner still matches.
    Result Rebind(const ReceiverAddress& receiver, const ProcessRef& old_owner, const ProcessRef& new_owner) override;
    // Removes a binding when owner/version still match the current entry.
    Result Invalidate(const ReceiverAddress& receiver, const ProcessRef& owner, std::uint64_t version) override;
    // Clears all local bindings when the hosting process tears down its runtime.
    void Clear();

private:
    struct Entry
    {
        ProcessRef owner;
        std::uint64_t version = 0;
    };

    static std::string MakeKey(const ReceiverAddress& receiver);

    mutable std::shared_mutex mMutex;
    std::unordered_map<std::string, Entry> mEntries;
};
} // namespace ipc
