#pragma once

#include "../../framework/ipc/receiver/receiver_host.h"

#include <atomic>
#include <string>

class ProcessReceiverHost final : public ipc::IReceiverHost
{
public:
    explicit ProcessReceiverHost(ipc::ProcessRef self)
        : mSelf(self)
    {
    }

    bool CanHandle(ipc::ReceiverType type) const override;
    ipc::DispatchResult Dispatch(const ipc::ReceiverAddress& target, const ipc::Envelope& envelope) override;

    std::uint64_t DispatchCount() const;
    const std::string& LastPayloadType() const;

private:
    ipc::ProcessRef mSelf;
    std::atomic<std::uint64_t> mDispatchCount = 0;
    std::string mLastPayloadType;
};
