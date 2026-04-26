#pragma once

#include "../base/envelope.h"
#include "../base/result.h"

namespace ipc
{
class IReceiverHost
{
public:
    virtual ~IReceiverHost() = default;

    virtual bool CanHandle(ReceiverType type) const = 0;
    virtual DispatchResult Dispatch(const ReceiverAddress& target, const Envelope& envelope) = 0;
};
} // namespace ipc
