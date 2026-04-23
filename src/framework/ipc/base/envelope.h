#pragma once

#include "receiver.h"

namespace ipc
{
enum class DeliverySemantic : std::uint8_t
{
    direct = 1,
    broadcast = 2
};

struct EnvelopeHeader
{
    ProcessRef source_process;
    DeliverySemantic semantic = DeliverySemantic::direct;
    ReceiverAddress target_receiver;
    RequestId request_id = 0;
    std::uint32_t flags = 0;
};

struct Envelope
{
    EnvelopeHeader header;
    BroadcastScope broadcast_scope;
    std::string payload_type_url;
    ByteBuffer payload_bytes;
};
} // namespace ipc
