#pragma once

#include "control_codec.h"
#include "link_view.h"

#include <unordered_map>

namespace ipc
{
class LinkManager final : public ILinkView
{
public:
    explicit LinkManager(ProcessRef self, std::uint32_t protocol_version = 1);

    void OnConnectionEvent(const ConnectionEvent& event);
    Result OnFrame(const RawFrame& frame);

    bool HasHealthyDirectLink(const ProcessRef& target) const override;
    std::vector<ProcessRef> GetHealthyLinks() const override;
    std::optional<ConnectionId> FindConnection(const ProcessRef& target) const;
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
