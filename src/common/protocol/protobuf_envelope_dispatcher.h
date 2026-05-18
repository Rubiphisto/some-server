#pragma once

#include "../../framework/ipc/base/envelope.h"
#include "../../framework/ipc/base/result.h"
#include "../../framework/ipc/messaging/payload_registry.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

namespace common::protocol
{
template <typename... ContextArgs>
class ProtobufEnvelopeDispatcher
{
public:
    using Handler = std::function<ipc::DispatchResult(const ipc::Envelope&, ContextArgs...)>;

    template <typename Message, typename HandlerClass>
    void Register(
        HandlerClass* instance,
        ipc::DispatchResult (HandlerClass::*handler)(const ipc::Envelope&, const Message&))
    {
        const auto type_url = ipc::PayloadRegistry::TypeUrlFor(Message{});
        mHandlers[type_url] = [instance, handler, type_url](const ipc::Envelope& envelope, ContextArgs...)
            -> ipc::DispatchResult {
            Message message;
            if (!message.ParseFromArray(
                    envelope.payload_bytes.data(),
                    static_cast<int>(envelope.payload_bytes.size())))
            {
                return ipc::DispatchResult::Failure("failed to parse protobuf envelope payload: " + type_url);
            }
            return (instance->*handler)(envelope, message);
        };
    }

    template <typename Message, typename HandlerClass>
    void Register(
        HandlerClass* instance,
        ipc::DispatchResult (HandlerClass::*handler)(ContextArgs..., const ipc::Envelope&, const Message&))
    {
        const auto type_url = ipc::PayloadRegistry::TypeUrlFor(Message{});
        mHandlers[type_url] = [instance, handler, type_url](const ipc::Envelope& envelope, ContextArgs... context_args)
            -> ipc::DispatchResult {
            Message message;
            if (!message.ParseFromArray(
                    envelope.payload_bytes.data(),
                    static_cast<int>(envelope.payload_bytes.size())))
            {
                return ipc::DispatchResult::Failure("failed to parse protobuf envelope payload: " + type_url);
            }
            return (instance->*handler)(context_args..., envelope, message);
        };
    }

    ipc::DispatchResult Dispatch(const ipc::Envelope& envelope, ContextArgs... context_args) const
    {
        const auto it = mHandlers.find(envelope.payload_type_url);
        if (it == mHandlers.end())
        {
            return ipc::DispatchResult::Success();
        }
        return it->second(envelope, context_args...);
    }

    [[nodiscard]] bool IsRegistered(const std::string& payload_type_url) const
    {
        return mHandlers.find(payload_type_url) != mHandlers.end();
    }

private:
    std::unordered_map<std::string, Handler> mHandlers;
};
}  // namespace common::protocol
