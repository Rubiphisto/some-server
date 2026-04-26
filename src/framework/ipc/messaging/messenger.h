#pragma once

#include "../base/envelope.h"
#include "../base/result.h"
#include "../receiver/receiver_directory.h"
#include "../receiver/receiver_registry.h"
#include "../routing/router.h"
#include "payload_registry.h"

#include <google/protobuf/message.h>

namespace ipc
{
class Messenger
{
public:
    Messenger(
        ProcessRef self,
        const Router& router,
        const IReceiverDirectory& receiver_directory,
        const ReceiverRegistry& receiver_registry,
        const PayloadRegistry& payload_registry)
        : mSelf(self)
        , mRouter(router)
        , mReceiverDirectory(receiver_directory)
        , mReceiverRegistry(receiver_registry)
        , mPayloadRegistry(payload_registry)
    {
    }

    SendResult SendToProcess(ProcessId target, const google::protobuf::Message& message) const;
    SendResult SendToReceiver(const ReceiverAddress& target, const google::protobuf::Message& message) const;
    SendResult Broadcast(const BroadcastScope& scope, const google::protobuf::Message& message) const;

private:
    static ReceiverAddress ProcessReceiver(ProcessId target);
    SendResult SendEnvelope(Envelope envelope, std::optional<ReceiverLocation> receiver_location) const;
    DispatchResult DispatchLocal(const Envelope& envelope) const;

    ProcessRef mSelf;
    const Router& mRouter;
    const IReceiverDirectory& mReceiverDirectory;
    const ReceiverRegistry& mReceiverRegistry;
    const PayloadRegistry& mPayloadRegistry;
};
} // namespace ipc
