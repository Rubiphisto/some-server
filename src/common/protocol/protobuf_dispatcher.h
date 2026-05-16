#pragma once

#include "../../framework/ipc/base/result.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

namespace common::protocol
{
template <typename... ContextArgs>
class ProtobufMessageDispatcher
{
public:
    using Handler = std::function<ipc::Result(const std::string&, ContextArgs...)>;

    template <typename Message, typename HandlerFn>
    void Register(const std::uint32_t message_id, HandlerFn&& handler)
    {
        auto callback = std::forward<HandlerFn>(handler);
        mHandlers[message_id] =
            [callback = std::move(callback)](const std::string& payload, ContextArgs... context_args) -> ipc::Result {
            Message message;
            if (!message.ParseFromString(payload))
            {
                return ipc::Result::Failure("failed to parse protobuf payload");
            }
            return callback(context_args..., message);
        };
    }

    ipc::Result Dispatch(const std::uint32_t message_id, const std::string& payload, ContextArgs... context_args) const
    {
        const auto it = mHandlers.find(message_id);
        if (it == mHandlers.end())
        {
            return ipc::Result::Failure("unsupported message id");
        }
        return it->second(payload, context_args...);
    }

    [[nodiscard]] bool IsRegistered(const std::uint32_t message_id) const
    {
        return mHandlers.find(message_id) != mHandlers.end();
    }

private:
    std::unordered_map<std::uint32_t, Handler> mHandlers;
};

template <typename... ContextArgs>
class ProtobufRequestResponseDispatcher
{
public:
    using Handler = std::function<ipc::Result(const std::string&, std::string&, ContextArgs...)>;

    template <typename Request, typename Response, typename HandlerFn>
    void Register(const std::uint32_t message_id, HandlerFn&& handler)
    {
        auto callback = std::forward<HandlerFn>(handler);
        mHandlers[message_id] = [callback = std::move(callback)](
                                    const std::string& payload,
                                    std::string& response_payload,
                                    ContextArgs... context_args) -> ipc::Result {
            Request request;
            if (!request.ParseFromString(payload))
            {
                return ipc::Result::Failure("failed to parse protobuf payload");
            }

            Response response;
            const auto result = callback(context_args..., request, response);
            if (!result.ok)
            {
                return result;
            }
            if (!response.SerializeToString(&response_payload))
            {
                return ipc::Result::Failure("failed to serialize protobuf payload");
            }
            return ipc::Result::Success();
        };
    }

    ipc::Result Dispatch(
        const std::uint32_t message_id,
        const std::string& payload,
        std::string& response_payload,
        ContextArgs... context_args) const
    {
        const auto it = mHandlers.find(message_id);
        if (it == mHandlers.end())
        {
            return ipc::Result::Failure("unsupported message id");
        }
        return it->second(payload, response_payload, context_args...);
    }

    [[nodiscard]] bool IsRegistered(const std::uint32_t message_id) const
    {
        return mHandlers.find(message_id) != mHandlers.end();
    }

private:
    std::unordered_map<std::uint32_t, Handler> mHandlers;
};
}  // namespace common::protocol
