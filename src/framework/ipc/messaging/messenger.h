#pragma once

#include "../base/envelope.h"
#include "../base/result.h"
#include "../receiver/receiver_directory.h"
#include "../receiver/receiver_registry.h"
#include "../discovery/membership_view.h"
#include "../link/link_view.h"
#include "../routing/router.h"
#include "payload_registry.h"
#include "remote_message_sender.h"

#include <google/protobuf/message.h>

namespace ipc
{
// High-level IPC facade used by applications to send and dispatch internal messages.
class Messenger
{
public:
    Messenger(
        ProcessRef self,
        const Router& router,
        const IReceiverDirectory& receiver_directory,
        const ReceiverRegistry& receiver_registry,
        const PayloadRegistry& payload_registry,
        const IMembershipView* membership = nullptr,
        const ILinkView* links = nullptr,
        const IRemoteMessageSender* remote_sender = nullptr)
        : mSelf(self)
        , mRouter(router)
        , mReceiverDirectory(receiver_directory)
        , mReceiverRegistry(receiver_registry)
        , mPayloadRegistry(payload_registry)
        , mMembership(membership)
        , mLinks(links)
        , mRemoteSender(remote_sender)
    {
    }

    // Sends one protobuf payload directly to a concrete process instance.
    SendResult SendToProcess(ProcessId target, const google::protobuf::Message& message) const;
    // Resolves target through the receiver directory and sends one logical message.
    SendResult SendToReceiver(const ReceiverAddress& target, const google::protobuf::Message& message) const;
    // Broadcasts one message to a receiver namespace across the given scope.
    SendResult BroadcastToReceiver(
        const ReceiverAddress& target,
        BroadcastScope scope,
        const google::protobuf::Message& message) const;
    // Convenience wrapper for first-phase service broadcast semantics.
    SendResult BroadcastToService(
        ServiceType service_type,
        const BroadcastScope& scope,
        const google::protobuf::Message& message) const;
    // Decodes one inbound data frame and dispatches or forwards its envelope.
    Result HandleIncomingFrame(const RawFrame& frame) const;

private:
    static ReceiverAddress ProcessReceiver(ProcessId target);
    SendResult SendEnvelope(Envelope envelope, std::optional<ReceiverLocation> receiver_location) const;
    DispatchResult DispatchLocal(const Envelope& envelope) const;

    ProcessRef mSelf;
    const Router& mRouter;
    const IReceiverDirectory& mReceiverDirectory;
    const ReceiverRegistry& mReceiverRegistry;
    const PayloadRegistry& mPayloadRegistry;
    const IMembershipView* mMembership = nullptr;
    const ILinkView* mLinks = nullptr;
    const IRemoteMessageSender* mRemoteSender = nullptr;
};
} // namespace ipc
