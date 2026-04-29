# Interprocess Communication Implementation Plan

## Status

- Draft
- Scope: first-phase implementation plan for the IPC design
- Depends on: `docs/interprocess-communication-design.md`

## Purpose

This document defines the first implementation slice of the IPC system.

The architecture and protocol baseline remain in:

- [interprocess-communication-design.md](/home/dev/projects/some-server/docs/interprocess-communication-design.md)

This document is intentionally separate because:

- the design document is the architectural baseline
- this document is the staged implementation plan
- implementation scope will likely evolve faster than the architecture baseline

## First-Phase Goal

The first phase must deliver one minimal but real end-to-end IPC path.

Required outcome:

- `relay` starts and registers into discovery
- `game` starts and registers into discovery
- `game` can send an internal message through the messaging facade
- routing selects relay when no direct path exists
- relay forwards to the target process
- the target process dispatches the message through a local receiver host

This phase is not trying to complete the full IPC system.

It is trying to prove:

- the layering is correct
- the protocol split is usable
- the relay-first baseline works
- receiver-aware dispatch works

## First-Phase Scope

### Must Be Implemented

- IPC base types
- internal protobuf schemas
- transport layer with fixed-size wire frame header
- link layer with minimal control-plane handshake
- discovery layer with etcd membership registration
- routing layer with `RelayFirstPolicy`
- local receiver directory
- receiver host registration and local dispatch
- messaging facade with minimal send APIs
- `relay` application
- `game` application IPC integration

### Must Not Be Implemented

- automatic direct-upgrade
- full mesh topology
- remote shared receiver directory
- `GroupReceiver`
- request/response RPC semantics
- retry orchestration
- dead-letter infrastructure
- advanced relay scoring
- cross-region federation
- advanced broadcast optimization

## First-Phase Receiver Scope

The first phase should implement these receiver categories in this order:

1. `Process`
2. `Service`
3. `Player`

Rules:

- `ProcessReceiver` is required
- `ServiceReceiver` is required
- `PlayerReceiver` is recommended in the first phase, not postponed indefinitely

Why this order:

- `ProcessReceiver` validates route-to-process behavior
- `ServiceReceiver` validates host-based dispatch
- `PlayerReceiver` validates ownership semantics

## First-Phase Applications

The first phase should integrate only these applications:

- `relay`
- `game`

### Why `relay + game`

- `relay` is the infrastructure forwarding role
- `game` is a low-risk first business application target in this repository
- `gate` would pull in extra concerns too early, especially external protocol and session semantics

The first phase should not start by integrating every application.

## Repository Layout

Recommended top-level additions:

```text
proto/
src/framework/ipc/
src/relay/
```

### Proto Layout

```text
proto/ipc/common/v1/types.proto
proto/ipc/control/v1/control.proto
proto/ipc/data/v1/envelope.proto
```

### Framework IPC Layout

```text
src/framework/ipc/
  base/
  transport/
  link/
  discovery/
  routing/
  receiver/
  messaging/
```

### Relay Application Layout

```text
src/relay/
  CMakeLists.txt
  relay.cmake
  pch.h
  main.cpp
  application.h
  application.cpp
  services/
    ipc_service.h
    ipc_service.cpp
```

### Game Integration Layout

```text
src/game/services/
  ipc_client_service.h
  ipc_client_service.cpp
```

## Module Breakdown

### `src/framework/ipc/base/`

Purpose:

- stable core types and result types

Suggested contents:

- `types.h`
- `process.h`
- `receiver.h`
- `envelope.h`
- `result.h`

Must not contain:

- etcd logic
- TCP logic
- protobuf transport logic

### `src/framework/ipc/transport/`

Purpose:

- TCP connections
- fixed-size frame header
- framed byte transport

Suggested contents:

- `frame.h`
- `transport.h`
- `tcp_transport.h`
- `tcp_transport.cpp`

### `src/framework/ipc/link/`

Purpose:

- link establishment
- `Hello / HelloAck / Ping / Pong / Close`
- remote process identity tracking
- `LinkView`

Suggested contents:

- `link.h`
- `link_manager.h`
- `link_manager.cpp`
- `link_view.h`
- `control_codec.h`
- `control_codec.cpp`

### `src/framework/ipc/discovery/`

Purpose:

- etcd-backed process membership
- register, keepalive, snapshot, watch
- `MembershipView`

Suggested contents:

- `discovery.h`
- `membership_view.h`
- `etcd_discovery.h`
- `etcd_discovery.cpp`

### `src/framework/ipc/routing/`

Purpose:

- route planning
- topology policy execution
- `RelayFirstPolicy`

Suggested contents:

- `route_plan.h`
- `routing_context.h`
- `topology_policy.h`
- `relay_first_policy.h`
- `relay_first_policy.cpp`
- `router.h`
- `router.cpp`

### `src/framework/ipc/receiver/`

Purpose:

- local receiver ownership
- receiver host registration
- local resolution

Suggested contents:

- `receiver_directory.h`
- `receiver_host.h`
- `local_receiver_directory.h`
- `local_receiver_directory.cpp`
- `receiver_registry.h`
- `receiver_registry.cpp`

### `src/framework/ipc/messaging/`

Purpose:

- application-facing send APIs
- payload registry
- local dispatch orchestration

Suggested contents:

- `messenger.h`
- `messenger.cpp`
- `payload_registry.h`
- `payload_registry.cpp`
- `dispatcher.h`
- `dispatcher.cpp`

## Library And Build Plan

The IPC implementation should be built as a dedicated framework library.

Recommended layout:

```text
src/framework/ipc/CMakeLists.txt
src/framework/ipc/ipc.cmake
```

Recommended build approach:

- build `ipc` as an independent static library
- `relay` links `ipc`
- `game` links `ipc`

Why:

- dependency boundaries stay explicit
- tests can target IPC independently
- non-IPC applications are not forced to pull in IPC internals

## Runtime Assembly

The runtime assembly should stay uniform across applications.

### Relay Assembly

```text
RelayApp
 -> IpcService
    -> PayloadRegistry
    -> ReceiverRegistry
    -> ReceiverDirectory
    -> Transport
    -> LinkManager
    -> Discovery
    -> Router
    -> Messenger
```

### Game Assembly

```text
GameApp
 -> IpcClientService
    -> PayloadRegistry
    -> ReceiverRegistry
    -> ReceiverDirectory
    -> Transport
    -> LinkManager
    -> Discovery
    -> Router
    -> Messenger
```

Difference:

- `relay` hosts forwarding responsibilities
- `game` hosts business receiver dispatch

The component stack should still remain structurally similar.

## Startup Order

The first-phase startup order should be:

1. `PayloadRegistry`
2. `ReceiverRegistry`
3. `ReceiverDirectory`
4. `Transport`
5. `LinkManager`
6. `Discovery`
7. `Router`
8. `Messenger`
9. application IPC ready

### Startup Rule

The process must not register into discovery until:

- local receiver hosts are registered
- payload decoding is ready
- transport and link handling can accept inbound peers

This avoids exposing a half-started process to the cluster.

## Shutdown Order

Shutdown should be the reverse of startup intent.

Recommended order:

1. stop accepting new send requests
2. messenger enters reject/drain mode
3. revoke or stop discovery membership
4. stop route resolution for new work
5. close links
6. close transport
7. invalidate local receiver ownership
8. clear local registries

## Ready States

The first phase should distinguish:

- `transport-ready`
- `ipc-ready`

### `transport-ready`

Meaning:

- transport can listen and accept traffic
- link layer can perform handshake

### `ipc-ready`

Meaning:

- discovery registration is active
- routing is operational
- messenger is open for application use

These two states must not be conflated.

## First End-To-End Path

The first required end-to-end validation path is:

- `game-1 -> relay-1 -> game-2`
- send semantic: `SendToProcess`
- receive side: local `ProcessReceiverHost`

### Why This Is The First Path

- proves relay-first topology
- proves routing and forwarding
- proves the data envelope path
- avoids mixing in receiver ownership complexity too early

## Second End-To-End Path

The second required validation path is:

- `game-1 -> relay-1 -> game-2`
- send semantic: `SendToReceiver(PlayerReceiver)`
- receive side: local `PlayerReceiverHost`

This path validates:

- receiver ownership
- local bind / rebind / invalidate behavior
- host-based business dispatch

## Broadcast Validation Timing

Broadcast should be validated after the single-target paths.

Validation order:

1. `SendToProcess`
2. `SendToReceiver(ServiceReceiver)`
3. `SendToReceiver(PlayerReceiver)`
4. `BroadcastToService`

This keeps debugging focused and avoids introducing fanout complexity too early.

First-phase broadcast constraint:

- keep the broadcast API receiver-oriented
- only `ServiceReceiver` broadcast is implemented in the first phase
- other receiver kinds should return a clear not-supported result

## First-Phase Runtime Commands

The first phase should add minimal runtime visibility commands where appropriate.

Suggested commands:

- `ipc_status`
- `ipc_members`
- `ipc_links`
- `ipc_topology`
- `ipc_metrics`
- `ipc_receivers` (optional in the first slice)

### Minimum Value

The most important early commands are:

- `ipc_status`
- `ipc_members`
- `ipc_links`

These are enough to inspect liveness, membership, and direct-link health during integration.
As first-phase recovery and auto-connect behavior harden, `ipc_topology` and
`ipc_metrics` become part of the practical baseline as well.

## Implementation Milestones

### Milestone 1: Communication Base

Implement:

- `base`
- `proto`
- `transport`
- `link`

Validate:

- process-to-process handshake
- ping/pong
- link rejection on incompatibility

### Milestone 2: Discovery And Routing

Implement:

- `discovery`
- `routing`
- `RelayFirstPolicy`

Validate:

- membership registration
- snapshot/watch
- route planning for local/direct/relay/unreachable

### Milestone 3: Receiver And Messaging

Implement:

- `receiver`
- `receiver host`
- `messaging`

Validate:

- local send to receiver
- bind / rebind / invalidate
- host-based dispatch

### Milestone 4: Application Integration

Implement:

- `relay`
- `game` IPC integration

Validate:

- `game -> relay -> game`
- `PlayerReceiver` dispatch path

## Unit Test Plan

### Transport

- fixed frame header encode/decode
- payload length validation
- invalid frame rejection
- multiple-frame boundary correctness

### Link

- successful `Hello -> HelloAck`
- incompatible version rejection
- duplicate process identity rejection
- `Ping / Pong`
- `Close` cleanup

### Discovery

- registration visibility
- lease expiry removes member
- snapshot + watch converges without requiring manual refresh
- new `IncarnationId` is treated as a new process instance

### Routing

- local target resolves to `LocalDelivery`
- healthy direct link resolves to direct path
- no direct but relay available resolves to relay path
- missing route resolves to `Unreachable`
- broadcast expands to multi-target plan

### Receiver

- `Bind -> Resolve`
- `Rebind` invalidates old owner
- `Invalidate` leads to unresolved result
- ownership version updates correctly
- `PlayerReceiver` maintains one authoritative owner

### Messaging

- `SendToProcess` produces the right envelope intent
- `SendToReceiver` resolves before routing
- local dispatch reaches the correct host
- unknown payload type is rejected safely

## Integration Test Plan

### Integration 1: Base Link

- two processes establish transport and link
- handshake succeeds
- ping/pong succeeds

### Integration 2: Relay Route

- `game-1` sends to `game-2`
- no direct link exists
- relay is used
- `game-2` receives successfully

### Integration 3: Receiver Dispatch

- `game-2` binds a `PlayerReceiver`
- `game-1` sends to that receiver
- relay assists delivery
- `PlayerManager` receives and dispatches correctly

### Integration 4: Service Broadcast

- `BroadcastToService` reaches the expected service receivers
- local inclusion remains controlled by broadcast scope

### Integration 5: Topology Convergence

- startup order `relay -> game` converges automatically
- startup order `game -> relay` converges automatically
- relay restart reconverges automatically
- snapshot + watch convergence does not require manual refresh

### Integration 6: Discovery Degraded And Recovery

- backend loss drives `relay` and `game` into degraded membership state
- outbound IPC send paths reject while degraded
- auto-connect and forwarding stop participating while degraded
- transient backend recovery re-registers discovery membership automatically
- snapshot, watch, and relay-first topology reconverge automatically after
  backend recovery

## Failure Cases To Validate Early

- protocol version mismatch
- duplicate `ProcessId`
- relay unavailable
- receiver not bound
- stale ownership version
- target process disappears after membership change

These are required early because the architecture must be correct outside the happy path too.

## Logging Requirements

The first phase should log at least:

- discovery register / revoke / lease lost
- link hello / hello_ack / rejection reason
- route resolution result
- relay forwarding choice
- receiver bind / rebind / invalidate
- local dispatch success / failure

This is necessary for integration efficiency, not optional polish.

## Execution Order

The recommended practical implementation order is:

1. `base`
2. `proto`
3. `transport`
4. `link`
5. `discovery`
6. `routing`
7. `receiver`
8. `messaging`
9. `relay`
10. `game`

This order follows the dependency chain and minimizes thrash.

## Exit Criteria For Phase 1

Phase 1 is complete when all of the following are true:

- `relay` and `game` can both join discovery
- `game-1 -> relay-1 -> game-2` works for `SendToProcess`
- `PlayerReceiver` ownership can be bound and used for delivery
- local receiver host dispatch works
- core failure cases are covered by tests
- no direct-upgrade or shared receiver directory was required to achieve correctness

## Recommended Next Step

The implementation described in this plan has now reached the first-phase
exit criteria in code.

Current state:

- `relay` and `game` join discovery through etcd-backed registration
- relay-first process messaging works
- `PlayerReceiver` ownership and delivery work
- `BroadcastToService` works for the first-phase scope
- discovery watch and application-level auto-connect are in place
- multi-process integration tests cover the main relay-first paths
- multi-process integration tests cover degraded discovery and transient backend
  recovery
- first-phase auto-connect rules are centralized behind a shared topology
  policy object
- runtime commands expose membership, links, receivers, topology, degraded
  state, forwarding counters, keepalive/recovery counters, watch restart
  counters, and auto-connect success/failure counters

Recommended next work should focus on:

- consistency review between design and implementation
- documenting first-phase topology glue such as auto-connect
- tightening observability and operational tooling
- only then deciding whether to deepen watch semantics or broaden feature scope
