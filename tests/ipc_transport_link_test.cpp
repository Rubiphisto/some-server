#include "framework/ipc/link/link_manager.h"
#include "framework/ipc/transport/frame.h"
#include "framework/ipc/transport/tcp_transport.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
void Require(const bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void FlushOutbound(ipc::TcpTransport& transport, ipc::LinkManager& link_manager)
{
    for (auto& frame : link_manager.DrainOutboundFrames())
    {
        const ipc::Result send_result = transport.Send(frame);
        Require(send_result.ok, "transport send");
    }
}

bool WaitUntil(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

std::uint16_t NextPort()
{
    static std::atomic<std::uint16_t> port{35100};
    return port.fetch_add(1);
}

void Wire(ipc::TcpTransport& transport, ipc::LinkManager& link_manager, std::atomic<int>& frame_failures)
{
    transport.SetConnectionEventHandler(
        [&transport, &link_manager](const ipc::ConnectionEvent& event) {
            link_manager.OnConnectionEvent(event);
            FlushOutbound(transport, link_manager);
        });
    transport.SetFrameHandler(
        [&transport, &link_manager, &frame_failures](const ipc::RawFrame& frame) {
            const ipc::Result result = link_manager.OnFrame(frame);
            if (!result.ok)
            {
                ++frame_failures;
            }
            FlushOutbound(transport, link_manager);
        });
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
    ipc::TcpTransport lhs_transport;
    ipc::TcpTransport rhs_transport;
    ipc::LinkManager lhs({{1, 1}, 100});
    ipc::LinkManager rhs({{1, 2}, 200});
    std::atomic<int> lhs_failures = 0;
    std::atomic<int> rhs_failures = 0;

    Wire(lhs_transport, lhs, lhs_failures);
    Wire(rhs_transport, rhs, rhs_failures);

    const std::uint16_t port = NextPort();
    Require(rhs_transport.Listen({"127.0.0.1", port}).ok, "rhs listen");
    Require(lhs_transport.Connect({"127.0.0.1", port}).ok, "lhs connect");

    Require(
        WaitUntil(
            [&] { return lhs.GetHealthyLinks().size() == 1 && rhs.GetHealthyLinks().size() == 1; },
            std::chrono::seconds(2)),
        "link handshake completion");
    Require(lhs_failures.load() == 0, "lhs frame failures");
    Require(rhs_failures.load() == 0, "rhs frame failures");

    const auto lhs_links = lhs.GetHealthyLinks();
    const auto rhs_links = rhs.GetHealthyLinks();
    Require(lhs_links.front().process_id.service_type == 1, "lhs remote service type");
    Require(lhs_links.front().process_id.instance_id == 2, "lhs remote instance");
    Require(rhs_links.front().process_id.service_type == 1, "rhs remote service type");
    Require(rhs_links.front().process_id.instance_id == 1, "rhs remote instance");
}

void TestLinkRejectsIncompatibleProtocol()
{
    ipc::TcpTransport lhs_transport;
    ipc::TcpTransport rhs_transport;
    ipc::LinkManager lhs({{1, 1}, 100}, 1);
    ipc::LinkManager rhs({{1, 2}, 200}, 2);
    std::atomic<int> lhs_failures = 0;
    std::atomic<int> rhs_failures = 0;

    Wire(lhs_transport, lhs, lhs_failures);
    Wire(rhs_transport, rhs, rhs_failures);

    const std::uint16_t port = NextPort();
    Require(rhs_transport.Listen({"127.0.0.1", port}).ok, "rhs listen");
    Require(lhs_transport.Connect({"127.0.0.1", port}).ok, "lhs connect");

    Require(
        WaitUntil(
            [&] { return lhs_failures.load() > 0 && rhs_failures.load() > 0; },
            std::chrono::seconds(2)),
        "incompatible rejection completion");
    Require(lhs.GetHealthyLinks().empty(), "lhs should have no active links");
    Require(rhs.GetHealthyLinks().empty(), "rhs should have no active links");
}
} // namespace

int main()
{
    try
    {
        TestFrameHeaderRoundTrip();
        TestLinkHelloHandshake();
        TestLinkRejectsIncompatibleProtocol();
        std::cout << "ipc_transport_link_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "ipc_transport_link_test: " << ex.what() << std::endl;
        return 1;
    }
}
