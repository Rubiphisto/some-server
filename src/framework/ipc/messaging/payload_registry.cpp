#include "payload_registry.h"

namespace ipc
{
Result PayloadRegistry::Register(const google::protobuf::Message& message)
{
    mTypeUrls.insert(TypeUrlFor(message));
    return Result::Success();
}

bool PayloadRegistry::IsRegistered(const std::string& payload_type_url) const
{
    return mTypeUrls.contains(payload_type_url);
}

std::string PayloadRegistry::TypeUrlFor(const google::protobuf::Message& message)
{
    return "type.googleapis.com/" + message.GetDescriptor()->full_name();
}
} // namespace ipc
