#pragma once

#include "types.h"

#include <string>

namespace ipc
{
struct ProcessId
{
    ServiceType service_type = 0;
    InstanceId instance_id = 0;

    friend bool operator==(const ProcessId&, const ProcessId&) = default;
};

struct ProcessRef
{
    ProcessId process_id;
    IncarnationId incarnation_id = 0;

    friend bool operator==(const ProcessRef&, const ProcessRef&) = default;
};

struct Endpoint
{
    std::string host;
    std::uint16_t port = 0;

    friend bool operator==(const Endpoint&, const Endpoint&) = default;
};

struct ProcessDescriptor
{
    ProcessRef process;
    std::string service_name;
    Endpoint listen_endpoint;
    std::uint32_t protocol_version = 0;
    std::uint64_t start_time_unix_ms = 0;
    StringMap labels;
    std::vector<ServiceType> relay_capabilities;
};
} // namespace ipc
