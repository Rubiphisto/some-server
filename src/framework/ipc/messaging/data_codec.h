#pragma once

#include "../base/envelope.h"
#include "../base/result.h"

namespace ipc
{
ByteBuffer EncodeDataEnvelope(const Envelope& envelope);
Result DecodeDataEnvelope(const ByteBuffer& bytes, Envelope& envelope);
} // namespace ipc
