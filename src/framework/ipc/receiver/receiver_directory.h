#pragma once

#include "../base/process.h"
#include "../base/receiver.h"
#include "../base/result.h"

namespace ipc
{
// Maps logical receivers to the process that currently owns them.
class IReceiverDirectory
{
public:
    virtual ~IReceiverDirectory() = default;

    // Resolves one receiver address into its current local/remote ownership view.
    virtual ReceiverLocation Resolve(const ReceiverAddress& receiver) const = 0;
    // Creates the first authoritative owner binding for a receiver.
    virtual Result Bind(const ReceiverAddress& receiver, const ProcessRef& owner) = 0;
    // Moves a receiver binding from old_owner to new_owner.
    virtual Result Rebind(const ReceiverAddress& receiver, const ProcessRef& old_owner, const ProcessRef& new_owner) = 0;
    // Invalidates a binding if the supplied owner/version still matches.
    virtual Result Invalidate(const ReceiverAddress& receiver, const ProcessRef& owner, std::uint64_t version) = 0;
};
} // namespace ipc
