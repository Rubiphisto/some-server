#pragma once

#include "receiver.h"

namespace ipc
{
// Distinguishes one-hop direct delivery from logical broadcast fanout.
enum class DeliverySemantic : std::uint8_t
{
    direct = 1,
    broadcast = 2
};

// Carries routing metadata that every internal payload needs during transit.
struct EnvelopeHeader
{
    ProcessRef source_process;
    DeliverySemantic semantic = DeliverySemantic::direct;
    ReceiverAddress target_receiver;
    std::optional<ProcessRef> resolved_target_process;
    RequestId request_id = 0;
    std::uint32_t flags = 0;
};

// Wraps one internal message payload together with its IPC routing metadata.
struct Envelope
{
    EnvelopeHeader header;
    BroadcastScope broadcast_scope;
    std::string payload_type_url;
    ByteBuffer payload_bytes;
};
} // namespace ipc
