#pragma once

#include "../base/process.h"
#include "../base/receiver.h"
#include "../base/result.h"

namespace ipc
{
class IReceiverDirectory
{
public:
    virtual ~IReceiverDirectory() = default;

    virtual ReceiverLocation Resolve(const ReceiverAddress& receiver) const = 0;
    virtual Result Bind(const ReceiverAddress& receiver, const ProcessRef& owner) = 0;
    virtual Result Rebind(const ReceiverAddress& receiver, const ProcessRef& old_owner, const ProcessRef& new_owner) = 0;
    virtual Result Invalidate(const ReceiverAddress& receiver, const ProcessRef& owner, std::uint64_t version) = 0;
};
} // namespace ipc
