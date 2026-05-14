#pragma once

#include "../../framework/ipc/receiver/receiver_host.h"

#include <atomic>
#include <functional>
#include <string>

class ProcessReceiverHost final : public ipc::IReceiverHost
{
public:
    using DispatchHandler = std::function<ipc::DispatchResult(const ipc::ReceiverAddress&, const ipc::Envelope&)>;

    explicit ProcessReceiverHost(ipc::ProcessRef self)
        : mSelf(self)
    {
    }

    bool CanHandle(ipc::ReceiverType type) const override;
    ipc::DispatchResult Dispatch(const ipc::ReceiverAddress& target, const ipc::Envelope& envelope) override;
    void SetDispatchHandler(DispatchHandler handler);

    std::uint64_t DispatchCount() const;
    const std::string& LastPayloadType() const;

private:
    ipc::ProcessRef mSelf;
    DispatchHandler mDispatchHandler;
    std::atomic<std::uint64_t> mDispatchCount = 0;
    std::string mLastPayloadType;
};
