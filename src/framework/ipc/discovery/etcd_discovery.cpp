#include "etcd_discovery.h"

#include <glaze/glaze.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
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

    struct JsonLeaseGrantResponse
    {
        std::uint64_t ID = 0;
    };

    bool ExtractNextJsonObject(std::string& buffer, std::string& object)
    {
        bool in_string = false;
        bool escape = false;
        int depth = 0;
        std::size_t start = std::string::npos;

        for (std::size_t index = 0; index < buffer.size(); ++index)
        {
            const char ch = buffer[index];
            if (start == std::string::npos)
            {
                if (ch == '{')
                {
                    start = index;
                    depth = 1;
                }
                continue;
            }

            if (in_string)
            {
                if (escape)
                {
                    escape = false;
                    continue;
                }
                if (ch == '\\')
                {
                    escape = true;
                    continue;
                }
                if (ch == '"')
                {
                    in_string = false;
                }
                continue;
            }

            if (ch == '"')
            {
                in_string = true;
                continue;
            }
            if (ch == '{')
            {
                ++depth;
                continue;
            }
            if (ch == '}')
            {
                --depth;
                if (depth == 0)
                {
                    object = buffer.substr(start, index - start + 1);
                    buffer.erase(0, index + 1);
                    return true;
                }
            }
        }

        if (start == std::string::npos && buffer.size() > 4096)
        {
            buffer.clear();
        }
        return false;
    }

    std::string ShellEscape(const std::string& value)
    {
        std::string escaped = "'";
        for (char ch : value)
        {
            if (ch == '\'')
            {
                escaped += "'\"'\"'";
            }
            else
            {
                escaped += ch;
            }
        }
        escaped += "'";
        return escaped;
    }

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

    std::string LeaseIdArg(std::uint64_t lease_id)
    {
        std::ostringstream stream;
        stream << std::hex << std::nouppercase << lease_id;
        return stream.str();
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

template <>
struct glz::meta<ipc::detail::JsonLeaseGrantResponse>
{
    using T = ipc::detail::JsonLeaseGrantResponse;
    static constexpr auto value = glz::object("ID", &T::ID);
};

namespace ipc
{
EtcdDiscovery::EtcdDiscovery(EtcdDiscoveryOptions options)
    : mOptions(std::move(options))
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

    const Result put = RunPut(MemberKey(self.process.process_id), SerializeDescriptor(self));
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
    std::scoped_lock lock(mMutex);
    if (mLeaseId == 0)
    {
        return Result::Failure("lease not initialized");
    }

    return RunLeaseKeepAliveOnce();
}

Result EtcdDiscovery::RefreshSnapshot()
{
    std::scoped_lock lock(mMutex);
    return RefreshSnapshotUnlocked();
}

Result EtcdDiscovery::StartWatch()
{
    {
        std::scoped_lock lock(mMutex);
        if (mWatchThread.joinable())
        {
            return Result::Success();
        }

        // Refresh before and immediately after the watch loop starts so membership
        // view initialization does not depend on a manual ipc_refresh command.
        if (const Result refresh = RefreshSnapshotUnlocked(); !refresh.ok)
        {
            return refresh;
        }
        mStopWatch = false;
    }

    mWatchThread = std::thread(&EtcdDiscovery::WatchLoop, this);

    std::scoped_lock lock(mMutex);
    return RefreshSnapshotUnlocked();
}

void EtcdDiscovery::StopWatch()
{
    {
        std::scoped_lock lock(mMutex);
        mStopWatch = true;
        StopWatchProcess();
    }
    mWatchWakeup.notify_all();
    if (mWatchThread.joinable())
    {
        mWatchThread.join();
    }
}

bool EtcdDiscovery::WatchRunning() const
{
    return mWatchRunning.load();
}

Result EtcdDiscovery::RefreshSnapshotUnlocked()
{
    std::string output;
    const Result get = RunGetPrefix(mOptions.prefix + "/members/", output);
    if (!get.ok)
    {
        return get;
    }

    detail::JsonGetResponse response;
    if (auto result = glz::read<glz::opts{.error_on_unknown_keys = false}>(response, output))
    {
        return Result::Failure(glz::format_error(result, output));
    }

    std::unordered_map<std::uint64_t, ProcessDescriptor> refreshed;
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

    detail::QueueDiffEvents(mProcesses, refreshed, mEvents);
    mProcesses = std::move(refreshed);
    return Result::Success();
}

Result EtcdDiscovery::Remove(const ProcessId& id)
{
    std::scoped_lock lock(mMutex);
    const Result del = RunDelete(MemberKey(id));
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
    std::string output;
    const std::string command =
        detail::ShellEscape(mOptions.etcdctl_path) + " --endpoints=" + detail::ShellEscape(EndpointsArg()) +
        " lease grant " + std::to_string(mOptions.lease_ttl_seconds) + " -w json 2>/dev/null";
    const Result run = RunCommand(command, output);
    if (!run.ok)
    {
        return run;
    }

    detail::JsonLeaseGrantResponse response;
    if (auto result = glz::read<glz::opts{.error_on_unknown_keys = false}>(response, output))
    {
        return Result::Failure(glz::format_error(result, output));
    }
    if (response.ID == 0)
    {
        return Result::Failure("etcd lease grant returned empty lease id");
    }

    mLeaseId = response.ID;
    return Result::Success();
}

Result EtcdDiscovery::StartWatchProcess()
{
    if (mWatchPipeFd != -1 || mWatchPid > 0)
    {
        return Result::Success();
    }

    int fds[2];
    if (pipe(fds) != 0)
    {
        return Result::Failure("failed to create watch pipe");
    }

    const pid_t pid = fork();
    if (pid == -1)
    {
        close(fds[0]);
        close(fds[1]);
        return Result::Failure("failed to fork watch process");
    }

    if (pid == 0)
    {
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        const std::string endpoints_arg = "--endpoints=" + EndpointsArg();
        const std::string watch_prefix = mOptions.prefix + "/members/";
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(mOptions.etcdctl_path.c_str()));
        argv.push_back(const_cast<char*>(endpoints_arg.c_str()));
        argv.push_back(const_cast<char*>("watch"));
        argv.push_back(const_cast<char*>(watch_prefix.c_str()));
        argv.push_back(const_cast<char*>("--prefix"));
        argv.push_back(const_cast<char*>("-w"));
        argv.push_back(const_cast<char*>("json"));
        argv.push_back(nullptr);
        execvp(mOptions.etcdctl_path.c_str(), argv.data());
        _exit(127);
    }

    close(fds[1]);
    mWatchPipeFd = fds[0];
    mWatchPid = pid;
    return Result::Success();
}

void EtcdDiscovery::StopWatchProcess()
{
    if (mWatchPid > 0)
    {
        kill(mWatchPid, SIGTERM);
        waitpid(mWatchPid, nullptr, 0);
        mWatchPid = -1;
    }
    if (mWatchPipeFd != -1)
    {
        close(mWatchPipeFd);
        mWatchPipeFd = -1;
    }
}

void EtcdDiscovery::WatchLoop()
{
    mWatchRunning.store(true);
    while (true)
    {
        int watch_pipe_fd = -1;
        Result start_result = Result::Success();
        {
            std::scoped_lock lock(mMutex);
            if (mStopWatch)
            {
                break;
            }

            start_result = StartWatchProcess();
            watch_pipe_fd = mWatchPipeFd;
        }

        if (!start_result.ok || watch_pipe_fd == -1)
        {
            mWatchRunning.store(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        mWatchRunning.store(true);

        std::string buffer;
        char chunk[256];
        while (true)
        {
            const ssize_t read_count = read(watch_pipe_fd, chunk, sizeof(chunk));
            if (read_count <= 0)
            {
                std::scoped_lock lock(mMutex);
                StopWatchProcess();
                (void)RefreshSnapshotUnlocked();
                break;
            }

            buffer.append(chunk, static_cast<std::size_t>(read_count));

            std::string object;
            while (detail::ExtractNextJsonObject(buffer, object))
            {
                std::scoped_lock lock(mMutex);
                if (mStopWatch)
                {
                    StopWatchProcess();
                    mWatchRunning.store(false);
                    return;
                }
                (void)RefreshSnapshotUnlocked();
            }
        }

        std::unique_lock lock(mMutex);
        if (mStopWatch)
        {
            break;
        }
        mWatchWakeup.wait_for(lock, std::chrono::milliseconds(500), [this] { return mStopWatch; });
    }

    std::scoped_lock lock(mMutex);
    StopWatchProcess();
    mWatchRunning.store(false);
}

std::string EtcdDiscovery::EndpointsArg() const
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < mOptions.endpoints.size(); ++index)
    {
        if (index > 0)
        {
            stream << ",";
        }
        stream << mOptions.endpoints[index];
    }
    return stream.str();
}

std::string EtcdDiscovery::MemberKey(const ProcessId& id) const
{
    return mOptions.prefix + "/members/" + std::to_string(id.service_type) + "/" + std::to_string(id.instance_id);
}

Result EtcdDiscovery::RunCommand(const std::string& command, std::string& output) const
{
    output.clear();
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr)
    {
        return Result::Failure("failed to start command");
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        output.append(buffer);
    }

    const int rc = pclose(pipe);
    if (rc != 0)
    {
        return Result::Failure("command failed: " + command + "\n" + output);
    }
    return Result::Success();
}

Result EtcdDiscovery::RunPut(const std::string& key, const std::string& value) const
{
    std::string output;
    std::string command =
        detail::ShellEscape(mOptions.etcdctl_path) + " --endpoints=" + detail::ShellEscape(EndpointsArg()) +
        " put " + detail::ShellEscape(key) + " " + detail::ShellEscape(value);
    if (mLeaseId != 0)
    {
        command += " --lease=" + detail::LeaseIdArg(mLeaseId);
    }
    command += " -w json 2>/dev/null";
    return RunCommand(command, output);
}

Result EtcdDiscovery::RunLeaseKeepAliveOnce() const
{
    std::string output;
    const std::string command =
        detail::ShellEscape(mOptions.etcdctl_path) + " --endpoints=" + detail::ShellEscape(EndpointsArg()) +
        " lease keep-alive " + detail::LeaseIdArg(mLeaseId) + " --once -w json 2>/dev/null";
    return RunCommand(command, output);
}

Result EtcdDiscovery::RunDelete(const std::string& key) const
{
    std::string output;
    const std::string command =
        detail::ShellEscape(mOptions.etcdctl_path) + " --endpoints=" + detail::ShellEscape(EndpointsArg()) +
        " del " + detail::ShellEscape(key) + " -w json 2>/dev/null";
    return RunCommand(command, output);
}

Result EtcdDiscovery::RunGetPrefix(const std::string& key_prefix, std::string& output) const
{
    const std::string command =
        detail::ShellEscape(mOptions.etcdctl_path) + " --endpoints=" + detail::ShellEscape(EndpointsArg()) +
        " get " + detail::ShellEscape(key_prefix) + " --prefix -w json 2>/dev/null";
    return RunCommand(command, output);
}
} // namespace ipc
