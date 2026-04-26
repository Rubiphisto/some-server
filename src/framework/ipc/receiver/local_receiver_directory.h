#pragma once

#include "receiver_directory.h"

#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace ipc
{
class LocalReceiverDirectory final : public IReceiverDirectory
{
public:
    ReceiverLocation Resolve(const ReceiverAddress& receiver) const override;
    Result Bind(const ReceiverAddress& receiver, const ProcessRef& owner) override;
    Result Rebind(const ReceiverAddress& receiver, const ProcessRef& old_owner, const ProcessRef& new_owner) override;
    Result Invalidate(const ReceiverAddress& receiver, const ProcessRef& owner, std::uint64_t version) override;

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
