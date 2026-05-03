#pragma once

#include "../base/envelope.h"
#include "../base/result.h"

namespace ipc
{
// Serializes one internal envelope into a data-plane frame payload.
ByteBuffer EncodeDataEnvelope(const Envelope& envelope);
// Parses one data-plane payload back into an internal envelope.
Result DecodeDataEnvelope(const ByteBuffer& bytes, Envelope& envelope);
} // namespace ipc
