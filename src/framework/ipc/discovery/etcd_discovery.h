#pragma once

#include "../base/result.h"
#include "membership_view.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ipc
{
struct EtcdDiscoveryOptions
{
    std::string etcdctl_path = "etcdctl";
    std::vector<std::string> endpoints{"127.0.0.1:2379"};
    std::string prefix = "/some_server/ipc/dev/local";
};

class EtcdDiscovery final : public IMembershipView
{
public:
    explicit EtcdDiscovery(EtcdDiscoveryOptions options);

    Result RegisterSelf(const ProcessDescriptor& self);
    Result RefreshSnapshot();
    Result Remove(const ProcessId& id);

    std::optional<ProcessDescriptor> Find(const ProcessId& id) const override;
    std::vector<ProcessDescriptor> FindByService(ServiceType type) const override;
    std::vector<ProcessDescriptor> All() const override;

private:
    static std::uint64_t MakeKey(const ProcessId& id);
    static std::string SerializeDescriptor(const ProcessDescriptor& process);
    static Result DeserializeDescriptor(const std::string& json, ProcessDescriptor& process);

    std::string EndpointsArg() const;
    std::string MemberKey(const ProcessId& id) const;
    Result RunCommand(const std::string& command, std::string& output) const;
    Result RunPut(const std::string& key, const std::string& value) const;
    Result RunDelete(const std::string& key) const;
    Result RunGetPrefix(const std::string& key_prefix, std::string& output) const;

    EtcdDiscoveryOptions mOptions;
    std::unordered_map<std::uint64_t, ProcessDescriptor> mProcesses;
};
} // namespace ipc
