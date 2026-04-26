#include "local_receiver_directory.h"

#include <mutex>
#include <sstream>

namespace ipc
{
ReceiverLocation LocalReceiverDirectory::Resolve(const ReceiverAddress& receiver) const
{
    std::shared_lock lock(mMutex);
    const auto it = mEntries.find(MakeKey(receiver));
    if (it == mEntries.end())
    {
        return ReceiverLocation{};
    }

    ReceiverLocation location;
    location.kind = ReceiverLocationKind::single_process;
    location.processes.push_back(it->second.owner);
    location.version = it->second.version;
    return location;
}

Result LocalReceiverDirectory::Bind(const ReceiverAddress& receiver, const ProcessRef& owner)
{
    std::scoped_lock lock(mMutex);
    auto& entry = mEntries[MakeKey(receiver)];
    if (entry.version != 0 && entry.owner != owner)
    {
        return Result::Failure("receiver already bound to another owner");
    }

    if (entry.version == 0)
    {
        entry.version = 1;
    }
    entry.owner = owner;
    return Result::Success();
}

Result LocalReceiverDirectory::Rebind(
    const ReceiverAddress& receiver,
    const ProcessRef& old_owner,
    const ProcessRef& new_owner)
{
    std::scoped_lock lock(mMutex);
    const auto key = MakeKey(receiver);
    const auto it = mEntries.find(key);
    if (it == mEntries.end())
    {
        return Result::Failure("receiver is not bound");
    }
    if (it->second.owner != old_owner)
    {
        return Result::Failure("receiver owner mismatch");
    }

    it->second.owner = new_owner;
    ++it->second.version;
    return Result::Success();
}

Result LocalReceiverDirectory::Invalidate(const ReceiverAddress& receiver, const ProcessRef& owner, const std::uint64_t version)
{
    std::scoped_lock lock(mMutex);
    const auto key = MakeKey(receiver);
    const auto it = mEntries.find(key);
    if (it == mEntries.end())
    {
        return Result::Failure("receiver is not bound");
    }
    if (it->second.owner != owner)
    {
        return Result::Failure("receiver owner mismatch");
    }
    if (it->second.version != version)
    {
        return Result::Failure("receiver version mismatch");
    }

    mEntries.erase(it);
    return Result::Success();
}

std::string LocalReceiverDirectory::MakeKey(const ReceiverAddress& receiver)
{
    std::ostringstream stream;
    stream << static_cast<std::uint16_t>(receiver.type) << ':'
           << receiver.key_hi << ':'
           << receiver.key_lo;
    return stream.str();
}
} // namespace ipc
