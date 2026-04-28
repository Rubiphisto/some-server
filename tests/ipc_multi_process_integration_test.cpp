#include <chrono>
#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace
{
using Clock = std::chrono::steady_clock;

void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

std::string ResolveExecutablePath(const std::string& name)
{
    const auto cwd = std::filesystem::current_path();
    const std::array<std::filesystem::path, 4> candidates{
        cwd / name,
        cwd / ".." / "bin" / name,
        cwd / ".." / name,
        std::filesystem::path(name),
    };

    for (const auto& candidate : candidates)
    {
        std::error_code ignored;
        if (std::filesystem::exists(candidate, ignored))
        {
            return candidate.lexically_normal().string();
        }
    }

    throw std::runtime_error("failed to resolve executable path for " + name);
}

std::uint16_t AllocateTcpPort()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        throw std::runtime_error("failed to create port allocation socket");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        close(fd);
        throw std::runtime_error("failed to bind port allocation socket");
    }

    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &length) != 0)
    {
        close(fd);
        throw std::runtime_error("failed to query allocated port");
    }

    const std::uint16_t port = ntohs(bound.sin_port);
    close(fd);
    return port;
}

class ScopedEtcd
{
public:
    ScopedEtcd(std::filesystem::path data_dir, std::uint16_t client_port, std::uint16_t peer_port)
        : mDataDir(std::move(data_dir))
        , mClientPort(client_port)
        , mPeerPort(peer_port)
    {
    }

    ~ScopedEtcd()
    {
        Stop();
        std::error_code ignored;
        std::filesystem::remove_all(mDataDir, ignored);
    }

    void Start()
    {
        const std::string client_url = "http://127.0.0.1:" + std::to_string(mClientPort);
        const std::string peer_url = "http://127.0.0.1:" + std::to_string(mPeerPort);
        const std::string initial_cluster = "ipc-test=" + peer_url;

        const pid_t pid = fork();
        if (pid == -1)
        {
            throw std::runtime_error("fork failed for etcd");
        }
        if (pid == 0)
        {
            execlp(
                "etcd",
                "etcd",
                "--name",
                "ipc-test",
                "--data-dir",
                mDataDir.c_str(),
                "--listen-client-urls",
                client_url.c_str(),
                "--advertise-client-urls",
                client_url.c_str(),
                "--listen-peer-urls",
                peer_url.c_str(),
                "--initial-advertise-peer-urls",
                peer_url.c_str(),
                "--initial-cluster",
                initial_cluster.c_str(),
                "--initial-cluster-state",
                "new",
                static_cast<char*>(nullptr));
            _exit(127);
        }

        mPid = pid;

        for (int attempt = 0; attempt < 50; ++attempt)
        {
            const std::string health_command =
                "etcdctl --endpoints=127.0.0.1:" + std::to_string(mClientPort) +
                " endpoint health >/dev/null 2>&1";
            const int rc = std::system(health_command.c_str());
            if (rc == 0)
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        throw std::runtime_error("etcd did not become healthy");
    }

    void Stop()
    {
        if (mPid > 0)
        {
            kill(mPid, SIGTERM);
            waitpid(mPid, nullptr, 0);
            mPid = -1;
        }
    }

private:
    pid_t mPid = -1;
    std::filesystem::path mDataDir;
    std::uint16_t mClientPort = 0;
    std::uint16_t mPeerPort = 0;
};

class ChildProcess
{
public:
    ChildProcess(std::string executable, std::filesystem::path config_path)
        : mExecutable(std::move(executable))
        , mConfigPath(std::move(config_path))
    {
    }

    ~ChildProcess()
    {
        Stop();
    }

    void Start()
    {
        int stdin_pipe[2];
        int stdout_pipe[2];
        if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0)
        {
            throw std::runtime_error("pipe creation failed");
        }

        const pid_t pid = fork();
        if (pid == -1)
        {
            throw std::runtime_error("fork failed");
        }
        if (pid == 0)
        {
            dup2(stdin_pipe[0], STDIN_FILENO);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stdout_pipe[1], STDERR_FILENO);
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);

            execl(
                mExecutable.c_str(),
                mExecutable.c_str(),
                "-c",
                mConfigPath.c_str(),
                static_cast<char*>(nullptr));
            _exit(127);
        }

        mPid = pid;
        mStdinFd = stdin_pipe[1];
        mStdoutFd = stdout_pipe[0];
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
    }

    void Stop()
    {
        if (mPid <= 0)
        {
            return;
        }

        if (mStdinFd >= 0)
        {
            try
            {
                Send("exit");
            }
            catch (const std::exception&)
            {
            }
            close(mStdinFd);
            mStdinFd = -1;
        }

        bool exited = false;
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            int status = 0;
            const pid_t result = waitpid(mPid, &status, WNOHANG);
            if (result == mPid)
            {
                exited = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!exited)
        {
            kill(mPid, SIGTERM);
            waitpid(mPid, nullptr, 0);
        }

        if (mStdoutFd >= 0)
        {
            close(mStdoutFd);
            mStdoutFd = -1;
        }
        mPid = -1;
    }

    void Send(const std::string& command) const
    {
        const std::string line = command + "\n";
        if (write(mStdinFd, line.data(), line.size()) < 0)
        {
            throw std::runtime_error("failed to write command to " + mExecutable);
        }
    }

    bool WaitFor(std::string_view needle, const std::chrono::milliseconds timeout)
    {
        const auto deadline = Clock::now() + timeout;
        while (Clock::now() < deadline)
        {
            if (mOutput.find(needle) != std::string::npos)
            {
                return true;
            }

            pollfd fd{mStdoutFd, POLLIN, 0};
            const int rc = poll(&fd, 1, 100);
            if (rc > 0 && (fd.revents & POLLIN))
            {
                char buffer[4096];
                const ssize_t bytes = read(mStdoutFd, buffer, sizeof(buffer));
                if (bytes > 0)
                {
                    mOutput.append(buffer, static_cast<std::size_t>(bytes));
                    continue;
                }
            }
        }

        return mOutput.find(needle) != std::string::npos;
    }

    const std::string& Output() const
    {
        return mOutput;
    }

private:
    std::string mExecutable;
    std::filesystem::path mConfigPath;
    pid_t mPid = -1;
    int mStdinFd = -1;
    int mStdoutFd = -1;
    std::string mOutput;
};

std::filesystem::path WriteRelayConfig(
    const std::filesystem::path& dir,
    const std::string& prefix,
    const std::uint16_t etcd_port,
    const std::uint16_t listen_port)
{
    const auto path = dir / "relay.json";
    std::ofstream out(path);
    out << "{\n"
           "  \"log\": {\n"
           "    \"file\": \"" << (dir / "relay.log").string() << "\",\n"
           "    \"error_file\": \"" << (dir / "relay.error.log").string() << "\",\n"
           "    \"console\": true\n"
           "  },\n"
           "  \"relay\": {\n"
           "    \"instance_id\": 1,\n"
           "    \"listen\": { \"host\": \"127.0.0.1\", \"port\": " << listen_port << " },\n"
           "    \"discovery\": {\n"
           "      \"endpoints\": [\"127.0.0.1:" << etcd_port << "\"],\n"
           "      \"prefix\": \"" << prefix << "\",\n"
           "      \"lease_ttl_seconds\": 4\n"
           "    }\n"
           "  }\n"
           "}\n";
    return path;
}

std::filesystem::path WriteGameConfig(
    const std::filesystem::path& dir,
    const std::string& file_name,
    const std::string& prefix,
    const std::uint32_t instance_id,
    const std::uint16_t port,
    const std::uint16_t etcd_port)
{
    const auto path = dir / file_name;
    std::ofstream out(path);
    out << "{\n"
           "  \"log\": {\n"
           "    \"file\": \"" << (dir / (file_name + ".log")).string() << "\",\n"
           "    \"error_file\": \"" << (dir / (file_name + ".error.log")).string() << "\",\n"
           "    \"console\": true\n"
           "  },\n"
           "  \"game\": {\n"
           "    \"instance_id\": " << instance_id << ",\n"
           "    \"listen\": { \"host\": \"127.0.0.1\", \"port\": " << port << " },\n"
           "    \"discovery\": {\n"
           "      \"endpoints\": [\"127.0.0.1:" << etcd_port << "\"],\n"
           "      \"prefix\": \"" << prefix << "\",\n"
           "      \"lease_ttl_seconds\": 4\n"
           "    }\n"
           "  }\n"
           "}\n";
    return path;
}

void WaitOrThrow(ChildProcess& process, std::string_view needle, const std::string& message)
{
    if (!process.WaitFor(needle, std::chrono::seconds(10)))
    {
        throw std::runtime_error(message + "\noutput:\n" + process.Output());
    }
}

void PollCommandUntil(
    ChildProcess& process,
    const std::string& command,
    std::string_view needle,
    const std::string& message)
{
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        process.Send(command);
        if (process.WaitFor(needle, std::chrono::milliseconds(500)))
        {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    throw std::runtime_error(message + "\noutput:\n" + process.Output());
}

void PollCommandUntilWithRetries(
    ChildProcess& process,
    const std::string& command,
    std::string_view needle,
    const std::string& message,
    const int attempts,
    const std::chrono::milliseconds wait_per_attempt)
{
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        process.Send(command);
        if (process.WaitFor(needle, wait_per_attempt))
        {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    throw std::runtime_error(message + "\noutput:\n" + process.Output());
}

void RunRelayFirstMessagingScenario(const bool relay_first)
{
    const auto suffix = std::to_string(Clock::now().time_since_epoch().count());
    const auto temp_dir = std::filesystem::temp_directory_path() / ("ipc_multi_" + suffix);
    std::filesystem::create_directories(temp_dir);
    const auto etcd_dir = temp_dir / "etcd";
    std::filesystem::create_directories(etcd_dir);

    const std::uint16_t etcd_port = AllocateTcpPort();
    const std::uint16_t peer_port = AllocateTcpPort();
    const std::uint16_t relay_port = AllocateTcpPort();
    const std::uint16_t game1_port = AllocateTcpPort();
    const std::uint16_t game2_port = AllocateTcpPort();

    ScopedEtcd etcd(etcd_dir, etcd_port, peer_port);
    etcd.Start();

    const std::string prefix = "/some_server/ipc/test/multi/" + suffix;
    const auto relay_config = WriteRelayConfig(temp_dir, prefix, etcd_port, relay_port);
    const auto game1_config = WriteGameConfig(temp_dir, "game1.json", prefix, 1, game1_port, etcd_port);
    const auto game2_config = WriteGameConfig(temp_dir, "game2.json", prefix, 2, game2_port, etcd_port);

    const std::string relay_executable = ResolveExecutablePath("relay");
    const std::string game_executable = ResolveExecutablePath("game");

    ChildProcess relay(relay_executable, relay_config);
    ChildProcess game1(game_executable, game1_config);
    ChildProcess game2(game_executable, game2_config);

    if (relay_first)
    {
        relay.Start();
        game1.Start();
        game2.Start();
    }
    else
    {
        game1.Start();
        game2.Start();
        relay.Start();
    }

    WaitOrThrow(relay, "> ", "relay did not reach command prompt");
    WaitOrThrow(game1, "> ", "game1 did not reach command prompt");
    WaitOrThrow(game2, "> ", "game2 did not reach command prompt");

    PollCommandUntil(relay, "ipc_status", "watch_running=true members=3", "relay watch did not converge");
    PollCommandUntil(game1, "ipc_status", "watch_running=true members=3", "game1 watch did not converge");
    PollCommandUntil(game2, "ipc_status", "watch_running=true members=3", "game2 watch did not converge");

    PollCommandUntil(relay, "ipc_links", "relay ipc links: count=2", "relay links not healthy");
    PollCommandUntil(game1, "ipc_links", "game ipc links: count=1", "game1 relay link not healthy");
    PollCommandUntil(game2, "ipc_links", "game ipc links: count=1", "game2 relay link not healthy");

    game1.Send("ipc_send_process 2 process-test");
    WaitOrThrow(game1, "game ipc process send: ok", "process send failed");
    PollCommandUntil(game2, "ipc_status", "process_dispatch_count=1", "process dispatch did not arrive");

    game2.Send("ipc_bind_player_local 1001");
    game1.Send("ipc_bind_player_remote 1001 2");
    WaitOrThrow(game2, "game ipc bind player local: ok", "local player bind failed");
    WaitOrThrow(game1, "game ipc bind player remote: ok", "remote player bind failed");

    game1.Send("ipc_send_player 1001 player-test");
    WaitOrThrow(game1, "game ipc player send: ok", "player send failed");
    PollCommandUntil(game2, "ipc_status", "player_dispatch_count=1", "player dispatch did not arrive");
    WaitOrThrow(game2, "last_player_id=1001", "player dispatch target mismatch");

    game1.Send("ipc_broadcast_service broadcast-test 1");
    WaitOrThrow(game1, "game ipc broadcast service: ok", "service broadcast failed");
    PollCommandUntil(game1, "ipc_status", "local_service_dispatch_count=1", "game1 service broadcast local dispatch missing");
    PollCommandUntil(game2, "ipc_status", "local_service_dispatch_count=1", "game2 service broadcast dispatch missing");
}

void TestRelayRestartReconnect()
{
    const auto suffix = std::to_string(Clock::now().time_since_epoch().count());
    const auto temp_dir = std::filesystem::temp_directory_path() / ("ipc_restart_" + suffix);
    std::filesystem::create_directories(temp_dir);
    const auto etcd_dir = temp_dir / "etcd";
    std::filesystem::create_directories(etcd_dir);

    const std::uint16_t etcd_port = AllocateTcpPort();
    const std::uint16_t peer_port = AllocateTcpPort();
    const std::uint16_t relay_port = AllocateTcpPort();
    const std::uint16_t game1_port = AllocateTcpPort();
    const std::uint16_t game2_port = AllocateTcpPort();

    ScopedEtcd etcd(etcd_dir, etcd_port, peer_port);
    etcd.Start();

    const std::string prefix = "/some_server/ipc/test/restart/" + suffix;
    const auto relay_config = WriteRelayConfig(temp_dir, prefix, etcd_port, relay_port);
    const auto game1_config = WriteGameConfig(temp_dir, "restart-game1.json", prefix, 1, game1_port, etcd_port);
    const auto game2_config = WriteGameConfig(temp_dir, "restart-game2.json", prefix, 2, game2_port, etcd_port);

    const std::string relay_executable = ResolveExecutablePath("relay");
    const std::string game_executable = ResolveExecutablePath("game");

    ChildProcess game1(game_executable, game1_config);
    ChildProcess game2(game_executable, game2_config);
    ChildProcess relay(relay_executable, relay_config);

    game1.Start();
    game2.Start();
    relay.Start();

    WaitOrThrow(relay, "> ", "relay did not reach command prompt");
    WaitOrThrow(game1, "> ", "restart game1 did not reach command prompt");
    WaitOrThrow(game2, "> ", "restart game2 did not reach command prompt");

    PollCommandUntil(relay, "ipc_links", "relay ipc links: count=2", "initial relay links not healthy");
    PollCommandUntil(game1, "ipc_links", "game ipc links: count=1", "initial game1 relay link not healthy");
    PollCommandUntil(game2, "ipc_links", "game ipc links: count=1", "initial game2 relay link not healthy");

    relay.Stop();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    ChildProcess restarted_relay(relay_executable, relay_config);
    restarted_relay.Start();
    WaitOrThrow(restarted_relay, "> ", "restarted relay did not reach command prompt");

    PollCommandUntil(restarted_relay, "ipc_status", "watch_running=true members=3", "restarted relay watch did not converge");
    PollCommandUntil(game1, "ipc_status", "watch_running=true members=3", "game1 watch did not reconverge after relay restart");
    PollCommandUntil(game2, "ipc_status", "watch_running=true members=3", "game2 watch did not reconverge after relay restart");

    PollCommandUntil(restarted_relay, "ipc_links", "relay ipc links: count=2", "restarted relay links not healthy");
    PollCommandUntil(game1, "ipc_links", "game ipc links: count=1", "game1 did not reconnect to relay");
    PollCommandUntil(game2, "ipc_links", "game ipc links: count=1", "game2 did not reconnect to relay");

    game1.Send("ipc_send_process 2 restart-process-test");
    WaitOrThrow(game1, "game ipc process send: ok", "process send after relay restart failed");
    PollCommandUntil(game2, "ipc_status", "process_dispatch_count=1", "process dispatch after relay restart did not arrive");
}

void TestDiscoveryDegradedBehavior()
{
    const auto suffix = std::to_string(Clock::now().time_since_epoch().count());
    const auto temp_dir = std::filesystem::temp_directory_path() / ("ipc_degraded_" + suffix);
    std::filesystem::create_directories(temp_dir);
    const auto etcd_dir = temp_dir / "etcd";
    std::filesystem::create_directories(etcd_dir);

    const std::uint16_t etcd_port = AllocateTcpPort();
    const std::uint16_t peer_port = AllocateTcpPort();
    const std::uint16_t relay_port = AllocateTcpPort();
    const std::uint16_t game1_port = AllocateTcpPort();
    const std::uint16_t game2_port = AllocateTcpPort();

    ScopedEtcd etcd(etcd_dir, etcd_port, peer_port);
    etcd.Start();

    const std::string prefix = "/some_server/ipc/test/degraded/" + suffix;
    const auto relay_config = WriteRelayConfig(temp_dir, prefix, etcd_port, relay_port);
    const auto game1_config = WriteGameConfig(temp_dir, "degraded-game1.json", prefix, 1, game1_port, etcd_port);
    const auto game2_config = WriteGameConfig(temp_dir, "degraded-game2.json", prefix, 2, game2_port, etcd_port);

    const std::string relay_executable = ResolveExecutablePath("relay");
    const std::string game_executable = ResolveExecutablePath("game");

    ChildProcess relay(relay_executable, relay_config);
    ChildProcess game1(game_executable, game1_config);
    ChildProcess game2(game_executable, game2_config);

    relay.Start();
    game1.Start();
    game2.Start();

    WaitOrThrow(relay, "> ", "degraded relay did not reach command prompt");
    WaitOrThrow(game1, "> ", "degraded game1 did not reach command prompt");
    WaitOrThrow(game2, "> ", "degraded game2 did not reach command prompt");

    PollCommandUntil(relay, "ipc_status", "watch_running=true members=3", "degraded relay watch did not converge");
    PollCommandUntil(game1, "ipc_status", "watch_running=true members=3", "degraded game1 watch did not converge");
    PollCommandUntil(game2, "ipc_status", "watch_running=true members=3", "degraded game2 watch did not converge");

    PollCommandUntil(relay, "ipc_links", "relay ipc links: count=2", "degraded relay links not healthy");
    PollCommandUntil(game1, "ipc_links", "game ipc links: count=1", "degraded game1 relay link not healthy");
    PollCommandUntil(game2, "ipc_links", "game ipc links: count=1", "degraded game2 relay link not healthy");

    etcd.Stop();

    PollCommandUntilWithRetries(
        relay,
        "ipc_status",
        "registered=false ipc_ready=false membership_degraded=true",
        "relay did not enter degraded discovery state",
        60,
        std::chrono::milliseconds(500));
    PollCommandUntilWithRetries(
        game1,
        "ipc_status",
        "registered=false ipc_ready=false membership_degraded=true",
        "game1 did not enter degraded discovery state",
        60,
        std::chrono::milliseconds(500));
    PollCommandUntilWithRetries(
        game2,
        "ipc_status",
        "registered=false ipc_ready=false membership_degraded=true",
        "game2 did not enter degraded discovery state",
        60,
        std::chrono::milliseconds(500));

    game1.Send("ipc_send_process 2 degraded-process-test");
    WaitOrThrow(game1, "game ipc process send failed: ipc is not active", "process send should fail while degraded");

    game1.Send("ipc_broadcast_service degraded-broadcast 1");
    WaitOrThrow(
        game1,
        "game ipc broadcast service failed: ipc is not active",
        "broadcast should fail while degraded");

    game1.Send("ipc_send_player 1001 degraded-player-test");
    WaitOrThrow(game1, "game ipc player send failed: ipc is not active", "player send should fail while degraded");
}

void TestRelayFirstMessaging()
{
    RunRelayFirstMessagingScenario(true);
    RunRelayFirstMessagingScenario(false);
    TestRelayRestartReconnect();
    TestDiscoveryDegradedBehavior();
}
} // namespace

int main()
{
    std::signal(SIGPIPE, SIG_IGN);
    try
    {
        TestRelayFirstMessaging();
        std::cout << "ipc_multi_process_integration_test: ok" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "ipc_multi_process_integration_test: " << ex.what() << std::endl;
        return 1;
    }
}
