#pragma once

#include "control_codec.h"
#include "link_view.h"

#include <unordered_map>

namespace ipc
{
// Owns process handshake state and converts transport frames into logical links.
class LinkManager final : public ILinkView
{
public:
    // Creates a link manager for one local process and protocol version.
    explicit LinkManager(ProcessRef self, std::uint32_t protocol_version = 1);

    // Applies connect/disconnect transport events to link state and handshake flow.
    void OnConnectionEvent(const ConnectionEvent& event);
    // Processes one inbound control frame and updates link state accordingly.
    Result OnFrame(const RawFrame& frame);

    // Reports whether target currently has a fully active direct link.
    bool HasHealthyDirectLink(const ProcessRef& target) const override;
    // Returns every remote process whose link is currently active.
    std::vector<ProcessRef> GetHealthyLinks() const override;
    // Finds the transport connection currently bound to the given remote process.
    std::optional<ConnectionId> FindConnection(const ProcessRef& target) const;
    // Drains control frames that should be sent by the transport next.
    std::vector<RawFrame> DrainOutboundFrames();

private:
    Result HandleHello(ConnectionId connection_id, const ByteBuffer& payload);
    bool IsProtocolCompatible(const HelloInfo& hello) const;
    void QueueFrame(ConnectionId connection_id, FrameKind kind, ByteBuffer payload);

    ProcessRef mSelf;
    std::uint32_t mProtocolVersion = 1;
    std::unordered_map<ConnectionId, Link> mLinks;
    std::vector<RawFrame> mOutboundFrames;
};
} // namespace ipc
