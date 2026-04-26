#include "transport_message_sender.h"

namespace ipc
{
SendResult TransportMessageSender::Send(const ProcessRef& next_hop, const Envelope& envelope) const
{
    const auto connection_id = mLinks.FindConnection(next_hop);
    if (!connection_id.has_value())
    {
        return SendResult::Failure("no healthy link to next hop");
    }

    RawFrame frame;
    frame.connection_id = *connection_id;
    frame.header.kind = FrameKind::data;
    frame.payload = EncodeDataEnvelope(envelope);
    frame.header.length = static_cast<std::uint32_t>(frame.payload.size());
    return mTransport.Send(frame);
}
} // namespace ipc
