#include "receiver_registry.h"

namespace ipc
{
Result ReceiverRegistry::Register(IReceiverHost& host, const ReceiverType type)
{
    const auto index = static_cast<std::size_t>(type);
    if (index >= mHosts.size())
    {
        return Result::Failure("receiver type out of range");
    }
    if (!host.CanHandle(type))
    {
        return Result::Failure("receiver host cannot handle requested type");
    }
    if (mHosts[index] != nullptr && mHosts[index] != &host)
    {
        return Result::Failure("receiver host already registered");
    }

    mHosts[index] = &host;
    return Result::Success();
}

DispatchResult ReceiverRegistry::Dispatch(const ReceiverAddress& target, const Envelope& envelope) const
{
    const auto index = static_cast<std::size_t>(target.type);
    if (index >= mHosts.size())
    {
        return DispatchResult::Failure("receiver type out of range");
    }
    if (mHosts[index] == nullptr)
    {
        return DispatchResult::Failure("receiver host not registered");
    }

    return mHosts[index]->Dispatch(target, envelope);
}
} // namespace ipc
