#include "service_receiver_host.h"

bool ServiceReceiverHost::CanHandle(const ipc::ReceiverType type) const
{
    return type == ipc::ReceiverType::service;
}

ipc::DispatchResult ServiceReceiverHost::Dispatch(const ipc::ReceiverAddress& target, const ipc::Envelope& envelope)
{
    if (target.type != ipc::ReceiverType::service)
    {
        return ipc::DispatchResult::Failure("unsupported receiver type");
    }
    if (target.key_hi != mServiceType)
    {
        return ipc::DispatchResult::Failure("service receiver target mismatch");
    }

    ++mDispatchCount;
    mLastPayloadType = envelope.payload_type_url;
    return ipc::DispatchResult::Success();
}

std::uint64_t ServiceReceiverHost::DispatchCount() const
{
    return mDispatchCount.load();
}

const std::string& ServiceReceiverHost::LastPayloadType() const
{
    return mLastPayloadType;
}
