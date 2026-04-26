#pragma once

#include "../base/envelope.h"
#include "../base/process.h"
#include "../base/result.h"

namespace ipc
{
class IRemoteMessageSender
{
public:
    virtual ~IRemoteMessageSender() = default;

    virtual SendResult Send(const ProcessRef& next_hop, const Envelope& envelope) const = 0;
};
} // namespace ipc
