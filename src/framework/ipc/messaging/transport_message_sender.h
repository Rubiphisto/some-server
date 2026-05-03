#pragma once

#include "data_codec.h"
#include "remote_message_sender.h"

#include "../link/link_manager.h"
#include "../transport/transport.h"

namespace ipc
{
// Remote sender that writes resolved envelopes onto existing transport links.
class TransportMessageSender final : public IRemoteMessageSender
{
public:
    // Binds sender to the transport and link manager used for next-hop resolution.
    TransportMessageSender(ITransport& transport, const LinkManager& links)
        : mTransport(transport)
        , mLinks(links)
    {
    }

    // Encodes and writes one envelope to the connection for next_hop.
    SendResult Send(const ProcessRef& next_hop, const Envelope& envelope) const override;

private:
    ITransport& mTransport;
    const LinkManager& mLinks;
};
} // namespace ipc
