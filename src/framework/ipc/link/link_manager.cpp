#include "link_manager.h"

namespace ipc
{
LinkManager::LinkManager(ProcessRef self, std::uint32_t protocol_version)
    : mSelf(std::move(self))
    , mProtocolVersion(protocol_version)
{
}

void LinkManager::OnConnectionEvent(const ConnectionEvent& event)
{
    switch (event.type)
    {
    case ConnectionEventType::connected: {
        Link& link = mLinks[event.connection_id];
        link.connection_id = event.connection_id;
        link.state = LinkState::handshaking;
        QueueFrame(event.connection_id, FrameKind::control, EncodeHello(mSelf, mProtocolVersion));
        break;
    }
    case ConnectionEventType::disconnected:
        mLinks.erase(event.connection_id);
        break;
    }
}

Result LinkManager::OnFrame(const RawFrame& frame)
{
    if (frame.header.kind != FrameKind::control)
    {
        return Result::Failure("unsupported frame kind");
    }

    ProtoControlMessage message;
    if (!DecodeControlMessage(frame.payload, message))
    {
        return Result::Failure("invalid control message");
    }

    const ControlMessageType type = GetControlMessageType(message);
    switch (type)
    {
    case ControlMessageType::hello:
        return HandleHello(frame.connection_id, frame.payload);
    case ControlMessageType::hello_ack: {
        auto it = mLinks.find(frame.connection_id);
        if (it == mLinks.end())
        {
            return Result::Failure("link not found");
        }
        ProtoHelloAckResult result = some_server::ipc::control::v1::HelloAck_Result_RESULT_UNSPECIFIED;
        const Result extracted = ExtractHelloAckResult(message, result);
        if (!extracted.ok)
        {
            return extracted;
        }
        if (result != some_server::ipc::control::v1::HelloAck_Result_RESULT_OK)
        {
            it->second.state = LinkState::closed;
            return Result::Failure("hello_ack rejected");
        }
        // At this stage hello_ack only confirms the handshake. The remote identity
        // has already been learned from the peer's hello payload.
        it->second.state = LinkState::active;
        return Result::Success();
    }
    case ControlMessageType::ping:
        QueueFrame(frame.connection_id, FrameKind::control, EncodePong());
        return Result::Success();
    case ControlMessageType::pong:
    case ControlMessageType::close:
        return Result::Success();
    }

    return Result::Failure("unknown control message");
}

bool LinkManager::HasHealthyDirectLink(const ProcessRef& target) const
{
    for (const auto& [_, link] : mLinks)
    {
        if (link.state == LinkState::active && link.remote_process == target)
        {
            return true;
        }
    }
    return false;
}

std::vector<ProcessRef> LinkManager::GetHealthyLinks() const
{
    std::vector<ProcessRef> links;
    for (const auto& [_, link] : mLinks)
    {
        if (link.state == LinkState::active)
        {
            links.push_back(link.remote_process);
        }
    }
    return links;
}

std::optional<ConnectionId> LinkManager::FindConnection(const ProcessRef& target) const
{
    for (const auto& [connection_id, link] : mLinks)
    {
        if (link.state == LinkState::active && link.remote_process == target)
        {
            return connection_id;
        }
    }
    return std::nullopt;
}

std::vector<RawFrame> LinkManager::DrainOutboundFrames()
{
    std::vector<RawFrame> frames = std::move(mOutboundFrames);
    mOutboundFrames.clear();
    return frames;
}

Result LinkManager::HandleHello(ConnectionId connection_id, const ByteBuffer& payload)
{
    ProtoControlMessage message;
    if (!DecodeControlMessage(payload, message))
    {
        return Result::Failure("invalid hello control message");
    }

    HelloInfo hello;
    const Result extracted = ExtractHelloInfo(message, hello);
    if (!extracted.ok)
    {
        return extracted;
    }
    if (!IsProtocolCompatible(hello))
    {
        Link& link = mLinks[connection_id];
        link.connection_id = connection_id;
        link.remote_process = hello.process_ref;
        link.state = LinkState::closed;
        QueueFrame(
            connection_id,
            FrameKind::control,
            EncodeHelloAck(
                mSelf,
                mProtocolVersion,
                some_server::ipc::control::v1::HelloAck_Result_RESULT_INCOMPATIBLE_VERSION));
        return Result::Failure("incompatible protocol version");
    }

    Link& link = mLinks[connection_id];
    link.connection_id = connection_id;
    link.remote_process = hello.process_ref;
    link.state = LinkState::active;
    QueueFrame(connection_id, FrameKind::control, EncodeHelloAck(mSelf, mProtocolVersion));
    return Result::Success();
}

bool LinkManager::IsProtocolCompatible(const HelloInfo& hello) const
{
    if (hello.protocol_version == 0)
    {
        return false;
    }

    if (hello.protocol_version < mProtocolVersion)
    {
        return false;
    }

    if (hello.min_supported_protocol_version > mProtocolVersion)
    {
        return false;
    }

    return true;
}

void LinkManager::QueueFrame(ConnectionId connection_id, FrameKind kind, ByteBuffer payload)
{
    RawFrame frame;
    frame.connection_id = connection_id;
    frame.header.kind = kind;
    frame.header.length = static_cast<std::uint32_t>(payload.size());
    frame.payload = std::move(payload);
    mOutboundFrames.push_back(std::move(frame));
}
} // namespace ipc
