#pragma once

#include "receiver_host.h"

#include <array>

namespace ipc
{
class ReceiverRegistry
{
public:
    Result Register(IReceiverHost& host, ReceiverType type);
    DispatchResult Dispatch(const ReceiverAddress& target, const Envelope& envelope) const;

private:
    static constexpr std::size_t kMaxReceiverTypes = 8;

    std::array<IReceiverHost*, kMaxReceiverTypes> mHosts{};
};
} // namespace ipc
