#include "etcd_discovery.h"

#include <glaze/glaze.hpp>

#include <array>
#include <chrono>
#include <thread>

namespace ipc
{
namespace detail
{
    struct SerializableProcessDescriptor
    {
        ServiceType service_type = 0;
        InstanceId instance_id = 0;
        IncarnationId incarnation_id = 0;
        std::string service_name;
        std::string host;
        std::uint16_t port = 0;
        std::uint32_t protocol_version = 0;
        std::uint64_t start_time_unix_ms = 0;
        std::vector<ServiceType> relay_capabilities;
        std::vector<std::pair<std::string, std::string>> labels;
    };

    struct JsonKv
    {
        std::string key;
        std::string value;
    };

    struct JsonGetResponse
    {
        std::vector<JsonKv> kvs;
    };

    std::string Base64Decode(const std::string& encoded)
    {
        static constexpr std::array<int, 256> kDecodeTable = [] {
            std::array<int, 256> table{};
            table.fill(-1);
            for (int i = 'A'; i <= 'Z'; ++i)
            {
                table[i] = i - 'A';
            }
            for (int i = 'a'; i <= 'z'; ++i)
            {
                table[i] = i - 'a' + 26;
            }
            for (int i = '0'; i <= '9'; ++i)
            {
                table[i] = i - '0' + 52;
            }
            table[static_cast<unsigned char>('+')] = 62;
            table[static_cast<unsigned char>('/')] = 63;
            return table;
        }();

        std::string decoded;
        int val = 0;
        int valb = -8;
        for (unsigned char ch : encoded)
        {
            if (ch == '=')
            {
                break;
            }
            const int d = kDecodeTable[ch];
            if (d == -1)
            {
                continue;
            }
            val = (val << 6) + d;
            valb += 6;
            if (valb >= 0)
            {
                decoded.push_back(static_cast<char>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return decoded;
    }

    void QueueDiffEvents(
        const std::unordered_map<std::uint64_t, ProcessDescriptor>& previous,
        const std::unordered_map<std::uint64_t, ProcessDescriptor>& current,
        std::vector<MembershipEvent>& events)
    {
        for (const auto& [key, process] : current)
        {
            const auto it = previous.find(key);
            if (it == previous.end())
            {
                events.push_back(MembershipEvent{MembershipEventType::added, process});
                continue;
            }

            if (!(it->second.process == process.process) ||
                !(it->second.listen_endpoint == process.listen_endpoint) ||
                it->second.protocol_version != process.protocol_version ||
                it->second.start_time_unix_ms != process.start_time_unix_ms ||
                it->second.service_name != process.service_name ||
                it->second.labels != process.labels ||
                it->second.relay_capabilities != process.relay_capabilities)
            {
                events.push_back(MembershipEvent{MembershipEventType::updated, process});
            }
        }

        for (const auto& [key, process] : previous)
        {
            if (!current.contains(key))
            {
                events.push_back(MembershipEvent{MembershipEventType::removed, process});
            }
        }
    }
} // namespace detail
} // namespace ipc

template <>
struct glz::meta<ipc::detail::SerializableProcessDescriptor>
{
    using T = ipc::detail::SerializableProcessDescriptor;
    static constexpr auto value = glz::object(
        "service_type",
        &T::service_type,
        "instance_id",
        &T::instance_id,
        "incarnation_id",
        &T::incarnation_id,
        "service_name",
        &T::service_name,
        "host",
        &T::host,
        "port",
        &T::port,
        "protocol_version",
        &T::protocol_version,
        "start_time_unix_ms",
        &T::start_time_unix_ms,
        "relay_capabilities",
        &T::relay_capabilities,
        "labels",
        &T::labels);
};

template <>
struct glz::meta<ipc::detail::JsonKv>
{
    using T = ipc::detail::JsonKv;
    static constexpr auto value = glz::object("key", &T::key, "value", &T::value);
};

template <>
struct glz::meta<ipc::detail::JsonGetResponse>
{
    using T = ipc::detail::JsonGetResponse;
    static constexpr auto value = glz::object("kvs", &T::kvs);
};

namespace ipc
{
EtcdDiscovery::EtcdDiscovery(EtcdDiscoveryOptions options)
    : mOptions(std::move(options))
    , mBackend(CreateEtcdctlDiscoveryBackend(mOptions))
{
}

EtcdDiscovery::EtcdDiscovery(EtcdDiscoveryOptions options, std::unique_ptr<IEtcdDiscoveryBackend> backend)
    : mOptions(std::move(options))
    , mBackend(std::move(backend))
{
}

EtcdDiscovery::~EtcdDiscovery()
{
    StopWatch();
    if (mSelf.has_value())
    {
        Remove(mSelf->process.process_id);
    }
}

Result EtcdDiscovery::RegisterSelf(const ProcessDescriptor& self)
{
    std::scoped_lock lock(mMutex);
    if (mOptions.lease_ttl_seconds > 0 && mLeaseId == 0)
    {
        const Result lease = GrantLease();
        if (!lease.ok)
        {
            return lease;
        }
    }

    const Result put = mBackend->Put(MemberKey(self.process.process_id), SerializeDescriptor(self), mLeaseId);
    if (!put.ok)
    {
        return put;
    }

    mSelf = self;
    mProcesses[MakeKey(self.process.process_id)] = self;
    return Result::Success();
}

Result EtcdDiscovery::KeepAliveOnce()
{
    std::uint64_t lease_id = 0;
    {
        std::scoped_lock lock(mMutex);
        lease_id = mLeaseId;
    }

    if (lease_id == 0)
    {
        return Result::Failure("lease not initialized");
    }

    return mBackend->KeepAliveOnce(lease_id);
}

Result EtcdDiscovery::RefreshSnapshot()
{
    std::string output;
    const Result get = mBackend->GetPrefix(mOptions.prefix + "/members/", output);
    if (!get.ok)
    {
        std::scoped_lock lock(mMutex);
        ++mRuntimeStats.snapshot_refresh_failure_count;
        return get;
    }

    std::unordered_map<std::uint64_t, ProcessDescriptor> refreshed;
    const Result parsed = ParseSnapshot(output, refreshed);
    if (!parsed.ok)
    {
        std::scoped_lock lock(mMutex);
        ++mRuntimeStats.snapshot_refresh_failure_count;
        return parsed;
    }

    ApplySnapshot(std::move(refreshed));
    return Result::Success();
}

Result EtcdDiscovery::StartWatch()
{
    {
        std::scoped_lock lock(mMutex);
        if (mWatchThread.joinable())
        {
            return Result::Success();
        }

        mStopWatch = false;
    }

    // Refresh before and immediately after the watch loop starts so membership
    // view initialization does not depend on a manual ipc_refresh command.
    if (const Result refresh = RefreshSnapshot(); !refresh.ok)
    {
        return refresh;
    }

    mWatchThread = std::thread(&EtcdDiscovery::WatchLoop, this);
    return RefreshSnapshot();
}

void EtcdDiscovery::StopWatch()
{
    {
        std::scoped_lock lock(mMutex);
        mStopWatch = true;
        mBackend->StopWatch();
    }
    mWatchWakeup.notify_all();
    if (mWatchThread.joinable())
    {
        mWatchThread.join();
    }
}

bool EtcdDiscovery::WatchRunning() const
{
    return mBackend->WatchRunning();
}

EtcdDiscoveryRuntimeStats EtcdDiscovery::RuntimeStats() const
{
    std::scoped_lock lock(mMutex);
    return mRuntimeStats;
}

Result EtcdDiscovery::ParseSnapshot(
    const std::string& output,
    std::unordered_map<std::uint64_t, ProcessDescriptor>& refreshed)
{
    detail::JsonGetResponse response;
    if (auto result = glz::read<glz::opts{.error_on_unknown_keys = false}>(response, output))
    {
        return Result::Failure(glz::format_error(result, output));
    }

    refreshed.clear();
    for (const detail::JsonKv& kv : response.kvs)
    {
        ProcessDescriptor descriptor;
        const Result decoded = DeserializeDescriptor(detail::Base64Decode(kv.value), descriptor);
        if (!decoded.ok)
        {
            return decoded;
        }
        refreshed[MakeKey(descriptor.process.process_id)] = std::move(descriptor);
    }

    return Result::Success();
}

void EtcdDiscovery::ApplySnapshot(std::unordered_map<std::uint64_t, ProcessDescriptor> refreshed)
{
    std::scoped_lock lock(mMutex);
    detail::QueueDiffEvents(mProcesses, refreshed, mEvents);
    mProcesses = std::move(refreshed);
}

Result EtcdDiscovery::Remove(const ProcessId& id)
{
    std::scoped_lock lock(mMutex);
    const Result del = mBackend->Delete(MemberKey(id));
    if (!del.ok)
    {
        return del;
    }

    mProcesses.erase(MakeKey(id));
    if (mSelf.has_value() && mSelf->process.process_id == id)
    {
        mSelf.reset();
        mLeaseId = 0;
    }
    return Result::Success();
}

std::vector<MembershipEvent> EtcdDiscovery::DrainEvents()
{
    std::scoped_lock lock(mMutex);
    std::vector<MembershipEvent> events = std::move(mEvents);
    mEvents.clear();
    return events;
}

std::optional<ProcessDescriptor> EtcdDiscovery::Find(const ProcessId& id) const
{
    std::scoped_lock lock(mMutex);
    const auto it = mProcesses.find(MakeKey(id));
    if (it == mProcesses.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::vector<ProcessDescriptor> EtcdDiscovery::FindByService(ServiceType type) const
{
    std::scoped_lock lock(mMutex);
    std::vector<ProcessDescriptor> processes;
    for (const auto& [_, process] : mProcesses)
    {
        if (process.process.process_id.service_type == type)
        {
            processes.push_back(process);
        }
    }
    return processes;
}

std::vector<ProcessDescriptor> EtcdDiscovery::All() const
{
    std::scoped_lock lock(mMutex);
    std::vector<ProcessDescriptor> processes;
    processes.reserve(mProcesses.size());
    for (const auto& [_, process] : mProcesses)
    {
        processes.push_back(process);
    }
    return processes;
}

std::uint64_t EtcdDiscovery::MakeKey(const ProcessId& id)
{
    return (static_cast<std::uint64_t>(id.service_type) << 32) | id.instance_id;
}

std::string EtcdDiscovery::SerializeDescriptor(const ProcessDescriptor& process)
{
    detail::SerializableProcessDescriptor serializable;
    serializable.service_type = process.process.process_id.service_type;
    serializable.instance_id = process.process.process_id.instance_id;
    serializable.incarnation_id = process.process.incarnation_id;
    serializable.service_name = process.service_name;
    serializable.host = process.listen_endpoint.host;
    serializable.port = process.listen_endpoint.port;
    serializable.protocol_version = process.protocol_version;
    serializable.start_time_unix_ms = process.start_time_unix_ms;
    serializable.relay_capabilities = process.relay_capabilities;
    serializable.labels = process.labels;

    auto json = glz::write_json(serializable);
    return json ? *json : std::string{};
}

Result EtcdDiscovery::DeserializeDescriptor(const std::string& json, ProcessDescriptor& process)
{
    detail::SerializableProcessDescriptor serializable;
    if (auto result = glz::read<glz::opts{.error_on_unknown_keys = false}>(serializable, json))
    {
        return Result::Failure(glz::format_error(result, json));
    }

    process.process = ProcessRef{
        ProcessId{serializable.service_type, serializable.instance_id},
        serializable.incarnation_id};
    process.service_name = serializable.service_name;
    process.listen_endpoint = Endpoint{serializable.host, serializable.port};
    process.protocol_version = serializable.protocol_version;
    process.start_time_unix_ms = serializable.start_time_unix_ms;
    process.relay_capabilities = std::move(serializable.relay_capabilities);
    process.labels = std::move(serializable.labels);
    return Result::Success();
}

Result EtcdDiscovery::GrantLease()
{
    return mBackend->GrantLease(mOptions.lease_ttl_seconds, mLeaseId);
}

void EtcdDiscovery::WatchLoop()
{
    mWatchRunning.store(true);
    while (true)
    {
        Result start_result = Result::Success();
        {
            std::scoped_lock lock(mMutex);
            if (mStopWatch)
            {
                break;
            }

            start_result = mBackend->StartWatchPrefix(mOptions.prefix + "/members/");
        }

        if (!start_result.ok)
        {
            {
                std::scoped_lock lock(mMutex);
                ++mRuntimeStats.watch_start_failure_count;
            }
            mWatchRunning.store(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        mWatchRunning.store(true);

        while (true)
        {
            const WatchPollResult poll_result = mBackend->WaitForWatchEvent();
            if (poll_result.kind == WatchPollKind::event)
            {
                (void)RefreshSnapshot();
                continue;
            }

            if (poll_result.kind == WatchPollKind::stopped)
            {
                std::scoped_lock lock(mMutex);
                mWatchRunning.store(false);
                return;
            }

            {
                std::scoped_lock lock(mMutex);
                if (mStopWatch)
                {
                    mBackend->StopWatch();
                    mWatchRunning.store(false);
                    return;
                }
                ++mRuntimeStats.watch_stream_closed_count;
                ++mRuntimeStats.watch_restart_count;
            }
            (void)RefreshSnapshot();
            mBackend->StopWatch();
            break;
        }

        std::unique_lock lock(mMutex);
        if (mStopWatch)
        {
            break;
        }
        mWatchWakeup.wait_for(lock, std::chrono::milliseconds(500), [this] { return mStopWatch; });
    }

    std::scoped_lock lock(mMutex);
    mBackend->StopWatch();
    mWatchRunning.store(false);
}

std::string EtcdDiscovery::MemberKey(const ProcessId& id) const
{
    return mOptions.prefix + "/members/" + std::to_string(id.service_type) + "/" + std::to_string(id.instance_id);
}

} // namespace ipc
