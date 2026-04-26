#include "process_receiver_host.h"

bool ProcessReceiverHost::CanHandle(const ipc::ReceiverType type) const
{
    return type == ipc::ReceiverType::process;
}

ipc::DispatchResult ProcessReceiverHost::Dispatch(const ipc::ReceiverAddress& target, const ipc::Envelope& envelope)
{
    if (target.type != ipc::ReceiverType::process)
    {
        return ipc::DispatchResult::Failure("unsupported receiver type");
    }
    if (target.key_hi != mSelf.process_id.service_type || target.key_lo != mSelf.process_id.instance_id)
    {
        return ipc::DispatchResult::Failure("process receiver target mismatch");
    }

    ++mDispatchCount;
    mLastPayloadType = envelope.payload_type_url;
    return ipc::DispatchResult::Success();
}

std::uint64_t ProcessReceiverHost::DispatchCount() const
{
    return mDispatchCount.load();
}

const std::string& ProcessReceiverHost::LastPayloadType() const
{
    return mLastPayloadType;
}
