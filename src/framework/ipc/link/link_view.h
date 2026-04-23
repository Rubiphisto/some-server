#pragma once

#include "link.h"

#include <vector>

namespace ipc
{
class ILinkView
{
public:
    virtual ~ILinkView() = default;

    virtual bool HasHealthyDirectLink(const ProcessRef& target) const = 0;
    virtual std::vector<ProcessRef> GetHealthyLinks() const = 0;
};
} // namespace ipc
