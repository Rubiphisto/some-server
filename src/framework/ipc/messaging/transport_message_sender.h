#pragma once

#include "data_codec.h"
#include "remote_message_sender.h"

#include "../link/link_manager.h"
#include "../transport/transport.h"

namespace ipc
{
class TransportMessageSender final : public IRemoteMessageSender
{
public:
    TransportMessageSender(ITransport& transport, const LinkManager& links)
        : mTransport(transport)
        , mLinks(links)
    {
    }

    SendResult Send(const ProcessRef& next_hop, const Envelope& envelope) const override;

private:
    ITransport& mTransport;
    const LinkManager& mLinks;
};
} // namespace ipc
