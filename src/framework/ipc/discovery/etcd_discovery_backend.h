#pragma once

#include "../base/result.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ipc
{
// Backend connection and timeout knobs shared by SDK discovery transport.
struct EtcdDiscoveryOptions
{
    std::vector<std::string> endpoints{"127.0.0.1:2379"};
    std::string prefix = "/some_server/ipc/dev/local";
    std::uint32_t lease_ttl_seconds = 5;
    std::uint32_t command_timeout_seconds = 3;
};

// Normalizes backend watch outcomes for the discovery coordinator.
enum class WatchPollKind : std::uint8_t
{
    event,
    stream_closed,
    stopped,
    error
};

// One backend watch poll result delivered back to the discovery layer.
struct WatchPollResult
{
    WatchPollKind kind = WatchPollKind::event;
    std::string message;
};

// Minimal backend contract EtcdDiscovery needs from any etcd transport.
class IEtcdDiscoveryBackend
{
public:
    virtual ~IEtcdDiscoveryBackend() = default;

    // Allocates or refreshes one lease id that discovery can attach to member keys.
    // ttl_seconds controls the server lease TTL; lease_id receives the granted id.
    virtual Result GrantLease(std::uint32_t ttl_seconds, std::uint64_t& lease_id) = 0;
    // Stores one member record at key, optionally attaching it to lease_id.
    virtual Result Put(const std::string& key, const std::string& value, std::uint64_t lease_id) = 0;
    // Performs one bounded keepalive round-trip for the given lease id.
    virtual Result KeepAliveOnce(std::uint64_t lease_id) = 0;
    // Removes one member record from the backend.
    virtual Result Delete(const std::string& key) = 0;
    // Reads all keys under key_prefix and serializes them into output for snapshot parsing.
    virtual Result GetPrefix(const std::string& key_prefix, std::string& output) = 0;
    // Starts a backend watch stream for one member prefix.
    virtual Result StartWatchPrefix(const std::string& key_prefix) = 0;
    // Waits for the next watch event, stream closure, stop signal, or backend error.
    virtual WatchPollResult WaitForWatchEvent() = 0;
    // Stops any active watch stream and unblocks pending waits.
    virtual void StopWatch() = 0;
    // Reports whether the backend still has an active watch stream.
    virtual bool WatchRunning() const = 0;
};

// Creates the production SDK-backed etcd discovery transport.
std::unique_ptr<IEtcdDiscoveryBackend> CreateEtcdSdkDiscoveryBackend(const EtcdDiscoveryOptions& options);
} // namespace ipc
