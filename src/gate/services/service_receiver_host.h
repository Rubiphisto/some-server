#pragma once

#include "../../framework/ipc/receiver/receiver_host.h"

#include <atomic>
#include <string>

class ServiceReceiverHost final : public ipc::IReceiverHost
{
public:
    explicit ServiceReceiverHost(ipc::ServiceType service_type)
        : mServiceType(service_type)
    {
    }

    bool CanHandle(ipc::ReceiverType type) const override;
    ipc::DispatchResult Dispatch(const ipc::ReceiverAddress& target, const ipc::Envelope& envelope) override;

    std::uint64_t DispatchCount() const;
    const std::string& LastPayloadType() const;

private:
    ipc::ServiceType mServiceType = 0;
    std::atomic<std::uint64_t> mDispatchCount = 0;
    std::string mLastPayloadType;
};
