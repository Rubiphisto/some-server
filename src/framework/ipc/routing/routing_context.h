#pragma once

#include "../base/envelope.h"
#include "../base/receiver.h"
#include "../discovery/membership_view.h"
#include "../link/link_view.h"

#include <optional>

namespace ipc
{
struct RoutingContext
{
    ProcessRef self;
    Envelope envelope;
    std::optional<ReceiverLocation> receiver_location;
    const IMembershipView* membership = nullptr;
    const ILinkView* links = nullptr;
};
} // namespace ipc
