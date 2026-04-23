#include "framework/ipc/link/link_manager.h"
#include "framework/ipc/transport/frame.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void TestFrameHeaderRoundTrip()
    {
        ipc::FrameHeader header;
        header.kind = ipc::FrameKind::data;
        header.length = 128;

        const auto bytes = ipc::SerializeFrameHeader(header);
        ipc::FrameHeader decoded;
        Require(ipc::DeserializeFrameHeader(bytes.data(), bytes.size(), decoded), "frame header decode");
        Require(decoded.magic == ipc::kFrameMagic, "frame magic");
        Require(decoded.version == ipc::kFrameVersion, "frame version");
        Require(decoded.kind == ipc::FrameKind::data, "frame kind");
        Require(decoded.length == 128, "frame length");
    }

    void TestLinkHelloHandshake()
    {
        ipc::LinkManager lhs({{1, 1}, 100});
        ipc::LinkManager rhs({{1, 2}, 200});

        lhs.OnConnectionEvent({ipc::ConnectionEventType::connected, 1});
        rhs.OnConnectionEvent({ipc::ConnectionEventType::connected, 1});

        auto lhs_frames = lhs.DrainOutboundFrames();
        auto rhs_frames = rhs.DrainOutboundFrames();
        Require(lhs_frames.size() == 1, "lhs hello frame");
        Require(rhs_frames.size() == 1, "rhs hello frame");

        Require(rhs.OnFrame(lhs_frames.front()).ok, "rhs receive hello");
        Require(lhs.OnFrame(rhs_frames.front()).ok, "lhs receive hello");

        rhs_frames = rhs.DrainOutboundFrames();
        lhs_frames = lhs.DrainOutboundFrames();
        Require(rhs_frames.size() == 1, "rhs hello_ack frame");
        Require(lhs_frames.size() == 1, "lhs hello_ack frame");

        Require(lhs.OnFrame(rhs_frames.front()).ok, "lhs receive hello_ack");
        Require(rhs.OnFrame(lhs_frames.front()).ok, "rhs receive hello_ack");

        const auto lhs_links = lhs.GetHealthyLinks();
        const auto rhs_links = rhs.GetHealthyLinks();
        Require(lhs_links.size() == 1, "lhs healthy link count");
        Require(rhs_links.size() == 1, "rhs healthy link count");
        Require(lhs_links.front().process_id.service_type == 1, "lhs remote service type");
        Require(lhs_links.front().process_id.instance_id == 2, "lhs remote instance");
        Require(rhs_links.front().process_id.service_type == 1, "rhs remote service type");
        Require(rhs_links.front().process_id.instance_id == 1, "rhs remote instance");
    }
} // namespace

int main()
{
    try
    {
        TestFrameHeaderRoundTrip();
        TestLinkHelloHandshake();
        std::cout << "ipc_transport_link_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "ipc_transport_link_test: " << ex.what() << std::endl;
        return 1;
    }
}
