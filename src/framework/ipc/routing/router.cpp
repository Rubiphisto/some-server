#include "router.h"

namespace ipc
{
RoutePlan Router::Resolve(const RoutingContext& context) const
{
    return mPolicy.Resolve(context);
}
} // namespace ipc
