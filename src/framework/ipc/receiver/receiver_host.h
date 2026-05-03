#pragma once

#include "../base/envelope.h"
#include "../base/result.h"

namespace ipc
{
// Dispatch target for one family of receivers, such as process, player, or service.
class IReceiverHost
{
public:
    virtual ~IReceiverHost() = default;

    // Returns whether this host owns dispatch for the given receiver type.
    virtual bool CanHandle(ReceiverType type) const = 0;
    // Delivers envelope to the concrete local receiver represented by target.
    virtual DispatchResult Dispatch(const ReceiverAddress& target, const Envelope& envelope) = 0;
};
} // namespace ipc
