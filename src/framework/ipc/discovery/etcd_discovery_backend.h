#pragma once

#include "../base/result.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ipc
{
struct EtcdDiscoveryOptions
{
    std::vector<std::string> endpoints{"127.0.0.1:2379"};
    std::string prefix = "/some_server/ipc/dev/local";
    std::uint32_t lease_ttl_seconds = 5;
    std::uint32_t command_timeout_seconds = 3;
};

enum class WatchPollKind : std::uint8_t
{
    event,
    stream_closed,
    stopped,
    error
};

struct WatchPollResult
{
    WatchPollKind kind = WatchPollKind::event;
    std::string message;
};

class IEtcdDiscoveryBackend
{
public:
    virtual ~IEtcdDiscoveryBackend() = default;

    virtual Result GrantLease(std::uint32_t ttl_seconds, std::uint64_t& lease_id) = 0;
    virtual Result Put(const std::string& key, const std::string& value, std::uint64_t lease_id) = 0;
    virtual Result KeepAliveOnce(std::uint64_t lease_id) = 0;
    virtual Result Delete(const std::string& key) = 0;
    virtual Result GetPrefix(const std::string& key_prefix, std::string& output) = 0;
    virtual Result StartWatchPrefix(const std::string& key_prefix) = 0;
    virtual WatchPollResult WaitForWatchEvent() = 0;
    virtual void StopWatch() = 0;
    virtual bool WatchRunning() const = 0;
};

std::unique_ptr<IEtcdDiscoveryBackend> CreateEtcdSdkDiscoveryBackend(const EtcdDiscoveryOptions& options);
} // namespace ipc
