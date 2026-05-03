#pragma once

#include "link.h"

#include <vector>

namespace ipc
{
// Read-only link state view consumed by routing and messaging.
class ILinkView
{
public:
    virtual ~ILinkView() = default;

    // Returns whether a healthy direct link already exists to target.
    virtual bool HasHealthyDirectLink(const ProcessRef& target) const = 0;
    // Returns every currently healthy direct process link.
    virtual std::vector<ProcessRef> GetHealthyLinks() const = 0;
};
} // namespace ipc
