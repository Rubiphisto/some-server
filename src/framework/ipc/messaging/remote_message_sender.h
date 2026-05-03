#pragma once

#include "../base/envelope.h"
#include "../base/process.h"
#include "../base/result.h"

namespace ipc
{
// Abstracts how messenger forwards one already-routed envelope to another process.
class IRemoteMessageSender
{
public:
    virtual ~IRemoteMessageSender() = default;

    // Sends envelope to the chosen next hop after routing has already resolved it.
    virtual SendResult Send(const ProcessRef& next_hop, const Envelope& envelope) const = 0;
};
} // namespace ipc
