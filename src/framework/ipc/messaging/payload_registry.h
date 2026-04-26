#pragma once

#include "../base/result.h"

#include <google/protobuf/message.h>

#include <string>
#include <unordered_set>

namespace ipc
{
class PayloadRegistry
{
public:
    Result Register(const google::protobuf::Message& message);
    bool IsRegistered(const std::string& payload_type_url) const;

    static std::string TypeUrlFor(const google::protobuf::Message& message);

private:
    std::unordered_set<std::string> mTypeUrls;
};
} // namespace ipc
