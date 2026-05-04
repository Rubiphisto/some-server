#pragma once

#include "receiver_host.h"

#include <array>

namespace ipc
{
// Small lookup table that maps receiver types to the local host that handles them.
class ReceiverRegistry
{
public:
    // Registers host as the dispatcher for one receiver type.
    Result Register(IReceiverHost& host, ReceiverType type);
    // Dispatches one local envelope to the host responsible for target.type.
    DispatchResult Dispatch(const ReceiverAddress& target, const Envelope& envelope) const;
    // Clears all host mappings during runtime teardown.
    void Clear();

private:
    static constexpr std::size_t kMaxReceiverTypes = 8;

    std::array<IReceiverHost*, kMaxReceiverTypes> mHosts{};
};
} // namespace ipc
