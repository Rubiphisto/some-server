#pragma once

#include "../base/result.h"

#include <google/protobuf/message.h>

#include <string>
#include <unordered_set>

namespace ipc
{
// Tracks which protobuf payload types a process is willing to decode and dispatch.
class PayloadRegistry
{
public:
    // Registers one protobuf message type for later send/receive validation.
    Result Register(const google::protobuf::Message& message);
    // Returns whether the given type URL is currently accepted locally.
    bool IsRegistered(const std::string& payload_type_url) const;
    // Clears all registered payload types during runtime teardown.
    void Clear();

    // Builds the canonical protobuf type URL for one message instance.
    static std::string TypeUrlFor(const google::protobuf::Message& message);

private:
    std::unordered_set<std::string> mTypeUrls;
};
} // namespace ipc
