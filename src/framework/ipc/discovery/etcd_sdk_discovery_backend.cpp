#include "etcd_discovery_backend.h"

#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>

namespace ipc
{
namespace
{
std::string EndpointsTarget(const std::vector<std::string>& endpoints)
{
    if (endpoints.empty())
    {
        return "127.0.0.1:2379";
    }

    // First implementation keeps parity with the existing single-cluster
    // behavior and uses the first configured endpoint for unary requests.
    return endpoints.front();
}

std::string PrefixRangeEnd(std::string prefix)
{
    for (std::ptrdiff_t index = static_cast<std::ptrdiff_t>(prefix.size()) - 1; index >= 0; --index)
    {
        unsigned char& ch = reinterpret_cast<unsigned char&>(prefix[static_cast<std::size_t>(index)]);
        if (ch != 0xFF)
        {
            ++ch;
            prefix.resize(static_cast<std::size_t>(index + 1));
            return prefix;
        }
    }
    return std::string(1, '\0');
}

std::string Base64Encode(std::string_view value)
{
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((value.size() + 2) / 3) * 4);

    std::size_t index = 0;
    while (index + 3 <= value.size())
    {
        const auto a = static_cast<unsigned char>(value[index++]);
        const auto b = static_cast<unsigned char>(value[index++]);
        const auto c = static_cast<unsigned char>(value[index++]);
        encoded.push_back(kAlphabet[(a >> 2) & 0x3F]);
        encoded.push_back(kAlphabet[((a & 0x03) << 4) | ((b >> 4) & 0x0F)]);
        encoded.push_back(kAlphabet[((b & 0x0F) << 2) | ((c >> 6) & 0x03)]);
        encoded.push_back(kAlphabet[c & 0x3F]);
    }

    const std::size_t remaining = value.size() - index;
    if (remaining == 1)
    {
        const auto a = static_cast<unsigned char>(value[index]);
        encoded.push_back(kAlphabet[(a >> 2) & 0x3F]);
        encoded.push_back(kAlphabet[(a & 0x03) << 4]);
        encoded.push_back('=');
        encoded.push_back('=');
    }
    else if (remaining == 2)
    {
        const auto a = static_cast<unsigned char>(value[index++]);
        const auto b = static_cast<unsigned char>(value[index]);
        encoded.push_back(kAlphabet[(a >> 2) & 0x3F]);
        encoded.push_back(kAlphabet[((a & 0x03) << 4) | ((b >> 4) & 0x0F)]);
        encoded.push_back(kAlphabet[(b & 0x0F) << 2]);
        encoded.push_back('=');
    }

    return encoded;
}

std::string JsonEscape(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char ch : value)
    {
        switch (ch)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (ch < 0x20)
            {
                std::ostringstream stream;
                stream << "\\u" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
                       << static_cast<int>(ch);
                escaped += stream.str();
            }
            else
            {
                escaped.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return escaped;
}

std::string GrpcStatusMessage(const char* operation, const grpc::Status& status)
{
    std::ostringstream stream;
    stream << operation << " failed: code=" << static_cast<int>(status.error_code())
           << " message=" << status.error_message();
    return stream.str();
}

class EtcdSdkDiscoveryBackend final : public IEtcdDiscoveryBackend
{
public:
    explicit EtcdSdkDiscoveryBackend(EtcdDiscoveryOptions options)
        : mOptions(std::move(options))
        , mChannel(grpc::CreateChannel(EndpointsTarget(mOptions.endpoints), grpc::InsecureChannelCredentials()))
        , mKvStub(etcdserverpb::KV::NewStub(mChannel))
        , mLeaseStub(etcdserverpb::Lease::NewStub(mChannel))
        , mWatchStub(etcdserverpb::Watch::NewStub(mChannel))
    {
    }

    ~EtcdSdkDiscoveryBackend() override
    {
        StopWatch();
    }

    Result GrantLease(const std::uint32_t ttl_seconds, std::uint64_t& lease_id) override
    {
        grpc::ClientContext context;
        ApplyDeadline(context);

        etcdserverpb::LeaseGrantRequest request;
        request.set_ttl(static_cast<std::int64_t>(ttl_seconds));

        etcdserverpb::LeaseGrantResponse response;
        const grpc::Status status = mLeaseStub->LeaseGrant(&context, request, &response);
        if (!status.ok())
        {
            return Result::Failure(GrpcStatusMessage("LeaseGrant", status));
        }
        if (response.id() == 0)
        {
            return Result::Failure("LeaseGrant returned empty lease id");
        }

        lease_id = static_cast<std::uint64_t>(response.id());
        return Result::Success();
    }

    Result Put(const std::string& key, const std::string& value, const std::uint64_t lease_id) override
    {
        grpc::ClientContext context;
        ApplyDeadline(context);

        etcdserverpb::PutRequest request;
        request.set_key(key);
        request.set_value(value);
        if (lease_id != 0)
        {
            request.set_lease(static_cast<std::int64_t>(lease_id));
        }

        etcdserverpb::PutResponse response;
        const grpc::Status status = mKvStub->Put(&context, request, &response);
        if (!status.ok())
        {
            return Result::Failure(GrpcStatusMessage("Put", status));
        }
        return Result::Success();
    }

    Result KeepAliveOnce(const std::uint64_t lease_id) override
    {
        grpc::ClientContext context;
        ApplyDeadline(context);

        auto stream = mLeaseStub->LeaseKeepAlive(&context);
        if (!stream)
        {
            return Result::Failure("LeaseKeepAlive failed to create stream");
        }

        etcdserverpb::LeaseKeepAliveRequest request;
        request.set_id(static_cast<std::int64_t>(lease_id));
        if (!stream->Write(request))
        {
            const grpc::Status status = stream->Finish();
            return Result::Failure(GrpcStatusMessage("LeaseKeepAlive.Write", status));
        }
        stream->WritesDone();

        etcdserverpb::LeaseKeepAliveResponse response;
        if (!stream->Read(&response))
        {
            const grpc::Status status = stream->Finish();
            return Result::Failure(GrpcStatusMessage("LeaseKeepAlive.Read", status));
        }

        const grpc::Status status = stream->Finish();
        if (!status.ok())
        {
            return Result::Failure(GrpcStatusMessage("LeaseKeepAlive", status));
        }
        if (response.id() == 0)
        {
            return Result::Failure("LeaseKeepAlive returned empty lease id");
        }
        return Result::Success();
    }

    Result Delete(const std::string& key) override
    {
        grpc::ClientContext context;
        ApplyDeadline(context);

        etcdserverpb::DeleteRangeRequest request;
        request.set_key(key);

        etcdserverpb::DeleteRangeResponse response;
        const grpc::Status status = mKvStub->DeleteRange(&context, request, &response);
        if (!status.ok())
        {
            return Result::Failure(GrpcStatusMessage("DeleteRange", status));
        }
        return Result::Success();
    }

    Result GetPrefix(const std::string& key_prefix, std::string& output) override
    {
        grpc::ClientContext context;
        ApplyDeadline(context);

        etcdserverpb::RangeRequest request;
        request.set_key(key_prefix);
        request.set_range_end(PrefixRangeEnd(key_prefix));

        etcdserverpb::RangeResponse response;
        const grpc::Status status = mKvStub->Range(&context, request, &response);
        if (!status.ok())
        {
            return Result::Failure(GrpcStatusMessage("Range", status));
        }

        output.clear();
        output.reserve(static_cast<std::size_t>(response.kvs_size()) * 96);
        output.append("{\"kvs\":[");
        for (int index = 0; index < response.kvs_size(); ++index)
        {
            const auto& kv = response.kvs(index);
            if (index > 0)
            {
                output.push_back(',');
            }
            output.append("{\"key\":\"");
            output.append(JsonEscape(Base64Encode(kv.key())));
            output.append("\",\"value\":\"");
            output.append(JsonEscape(Base64Encode(kv.value())));
            output.append("\"}");
        }
        output.append("]}");
        return Result::Success();
    }

    Result StartWatchPrefix(const std::string& key_prefix) override
    {
        std::scoped_lock lock(mWatchMutex);
        if (mWatchRunning)
        {
            return Result::Success();
        }

        mWatchStopRequested = false;
        mWatchQueue.clear();
        mWatchThread = std::thread(&EtcdSdkDiscoveryBackend::WatchLoop, this, key_prefix);
        mWatchRunning = true;
        return Result::Success();
    }

    WatchPollResult WaitForWatchEvent() override
    {
        std::unique_lock lock(mWatchMutex);
        mWatchCv.wait(lock, [this] { return !mWatchQueue.empty() || !mWatchRunning; });
        if (!mWatchQueue.empty())
        {
            WatchPollResult result = std::move(mWatchQueue.front());
            mWatchQueue.pop_front();
            return result;
        }
        return WatchPollResult{WatchPollKind::stopped, {}};
    }

    void StopWatch() override
    {
        std::thread watch_thread;
        {
            std::scoped_lock lock(mWatchMutex);
            mWatchStopRequested = true;
            if (mWatchContext != nullptr)
            {
                mWatchContext->TryCancel();
            }
            if (mWatchThread.joinable())
            {
                watch_thread = std::move(mWatchThread);
            }
            mWatchRunning = false;
            mWatchQueue.push_back(WatchPollResult{WatchPollKind::stopped, {}});
        }
        mWatchCv.notify_all();
        if (watch_thread.joinable())
        {
            watch_thread.join();
        }
    }

    bool WatchRunning() const override
    {
        std::scoped_lock lock(mWatchMutex);
        return mWatchRunning;
    }

private:
    void ApplyDeadline(grpc::ClientContext& context) const
    {
        if (mOptions.command_timeout_seconds == 0)
        {
            return;
        }

        context.set_deadline(
            std::chrono::system_clock::now() + std::chrono::seconds(mOptions.command_timeout_seconds));
    }

    void PushWatchResult(WatchPollResult result)
    {
        {
            std::scoped_lock lock(mWatchMutex);
            mWatchQueue.push_back(std::move(result));
        }
        mWatchCv.notify_all();
    }

    void WatchLoop(std::string key_prefix)
    {
        auto context = std::make_unique<grpc::ClientContext>();
        std::shared_ptr<grpc::ClientReaderWriter<etcdserverpb::WatchRequest, etcdserverpb::WatchResponse>> stream =
            mWatchStub->Watch(context.get());
        {
            std::scoped_lock lock(mWatchMutex);
            mWatchContext = context.get();
        }

        if (!stream)
        {
            {
                std::scoped_lock lock(mWatchMutex);
                mWatchContext = nullptr;
                mWatchRunning = false;
            }
            PushWatchResult(WatchPollResult{WatchPollKind::error, "Watch failed to create stream"});
            return;
        }

        etcdserverpb::WatchRequest create_request;
        auto* watch_create = create_request.mutable_create_request();
        watch_create->set_key(key_prefix);
        watch_create->set_range_end(PrefixRangeEnd(key_prefix));
        if (!stream->Write(create_request))
        {
            const grpc::Status status = stream->Finish();
            {
                std::scoped_lock lock(mWatchMutex);
                mWatchContext = nullptr;
                mWatchRunning = false;
            }
            PushWatchResult(WatchPollResult{WatchPollKind::error, GrpcStatusMessage("Watch.WriteCreate", status)});
            return;
        }
        stream->WritesDone();

        bool emitted_terminal = false;
        etcdserverpb::WatchResponse response;
        while (stream->Read(&response))
        {
            if (response.canceled())
            {
                PushWatchResult(WatchPollResult{WatchPollKind::stream_closed, {}});
                emitted_terminal = true;
                break;
            }
            if (response.events_size() > 0)
            {
                PushWatchResult(WatchPollResult{WatchPollKind::event, {}});
            }
        }

        const grpc::Status status = stream->Finish();
        const bool stop_requested = [this] {
            std::scoped_lock lock(mWatchMutex);
            return mWatchStopRequested;
        }();

        {
            std::scoped_lock lock(mWatchMutex);
            mWatchContext = nullptr;
            mWatchRunning = false;
        }

        if (stop_requested)
        {
            PushWatchResult(WatchPollResult{WatchPollKind::stopped, {}});
            return;
        }

        if (emitted_terminal)
        {
            return;
        }

        if (status.ok() || status.error_code() == grpc::StatusCode::CANCELLED)
        {
            PushWatchResult(WatchPollResult{WatchPollKind::stream_closed, {}});
            return;
        }

        PushWatchResult(WatchPollResult{WatchPollKind::error, GrpcStatusMessage("Watch", status)});
    }

    EtcdDiscoveryOptions mOptions;
    std::shared_ptr<grpc::Channel> mChannel;
    std::unique_ptr<etcdserverpb::KV::Stub> mKvStub;
    std::unique_ptr<etcdserverpb::Lease::Stub> mLeaseStub;
    std::unique_ptr<etcdserverpb::Watch::Stub> mWatchStub;
    mutable std::mutex mWatchMutex;
    std::condition_variable mWatchCv;
    std::deque<WatchPollResult> mWatchQueue;
    std::thread mWatchThread;
    grpc::ClientContext* mWatchContext = nullptr;
    bool mWatchRunning = false;
    bool mWatchStopRequested = false;
};
} // namespace

std::unique_ptr<IEtcdDiscoveryBackend> CreateEtcdSdkDiscoveryBackend(const EtcdDiscoveryOptions& options)
{
    return std::make_unique<EtcdSdkDiscoveryBackend>(options);
}
} // namespace ipc
