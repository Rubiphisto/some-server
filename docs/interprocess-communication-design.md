# Interprocess Communication Design

## Status

- Draft
- Scope: process-to-process communication foundation inside this repository
- Current step: architecture layering and core identity/object model

## Goals

- Provide a process communication foundation that is independent from external client-facing protocols.
- Keep service discovery, transport, routing, and receiver semantics clearly separated.
- Hide topology details from application code.
- Support multiple instances per application.
- Support two primary send semantics:
  - send to a specific process
  - broadcast
- Make room for higher-level receiver concepts such as player, system, service, or group.
- Allow a hybrid topology:
  - relay-first as the default path
  - direct-connect as an optimization path

## Non-Goals For The First Phase

- Cross-region or cross-datacenter routing
- Full-blown group membership protocols
- Rich load-balancing policy matrix
- Transparent hot-path optimization for every traffic type
- Tight coupling with any single discovery backend client library

## Design Principles

- KISS first: the first usable version must be relay-first and predictable.
- Clear layering: each layer owns one kind of complexity.
- Stable upper APIs: topology changes must not force application changes.
- Identity first: process identity and receiver identity must be explicit and stable.
- Control plane and data plane must be separated.
- C++ implementation must be backend-adaptable: etcd is the current preferred backend, not a hardwired architectural dependency.
- Fixed-size wire framing may use a native C++ struct layout locally, but protocol payloads must use protobuf.

## Terminology

- Service: a logical application type such as `gate`, `game`, `match`, or `relay`.
- Process: one running instance of a service.
- Process descriptor: immutable or slowly changing metadata that describes a process instance.
- Link: a logical process-to-process communication relationship built on top of a transport connection.
- Receiver: a logical message target above process level.
- Control plane: discovery, registration, liveness, topology, route metadata.
- Data plane: process-to-process message transfer.

## Layering

Dependency direction must remain bottom-up only.

```text
Messaging Facade
    |
Receiver Directory ---- Routing
    |                     |
    +---------------------+
              |
          Discovery
              |
             Link
              |
          Transport
```

### 1. Transport Layer

Responsibilities:

- TCP listen, connect, send, close
- frame read/write
- heartbeats and timeouts at connection level if needed
- backpressure and send queue ownership

Must not know:

- discovery backend
- process identity
- receiver identity
- routing decisions
- protobuf business messages

Output:

- raw frames
- connection lifecycle events

### 2. Link Layer

Responsibilities:

- upgrade transport connections into process links
- perform internal handshake and version negotiation
- authenticate or validate remote process identity if required
- maintain link state
- exchange internal control messages such as hello, register, ping, bye

Must not know:

- discovery storage layout
- receiver ownership
- application routing policy

Output:

- link established / link lost events
- remote process identity
- validated control messages

### 3. Discovery Layer

Responsibilities:

- register the local process into a discovery backend
- renew liveness
- watch membership changes
- expose process snapshots and membership events

Must not know:

- which peers should be connected directly
- how a message should be routed
- receiver mapping

Output:

- membership snapshot
- membership add/update/remove events

### 4. Routing Layer

Responsibilities:

- resolve how a message should move through the topology
- choose local delivery, direct link, relay hop, or broadcast fanout
- own topology policy selection
- hide topology differences from upper layers

Must not know:

- application-specific receiver lifecycle
- discovery backend protocol details

Output:

- resolved route
- next-hop process

### 5. Receiver Directory Layer

Responsibilities:

- maintain mapping from receiver addresses to process locations
- support lookup, bind, migrate, and invalidate
- abstract higher-level targets such as player, system, or service

Must not know:

- transport connection details
- etcd key layout

Output:

- receiver location or receiver group expansion

### 6. Messaging Facade Layer

Responsibilities:

- expose a stable application-facing messaging API
- wrap business protobuf messages into internal envelopes
- dispatch inbound envelopes to registered handlers

Must not know:

- direct TCP connection details
- discovery backend specifics
- relay implementation details

## Cross-Layer Rules

The following concepts are allowed to cross layer boundaries:

- `ServiceType`
- `ProcessId`
- `InstanceId`
- `IncarnationId`
- `ProcessDescriptor`
- `ReceiverAddress`
- `BroadcastScope`
- `Envelope`
- `Route`

The following concepts must not escape their owning layer:

- `ConnectionId` must not be visible to application code.
- discovery backend paths or keys must not be visible above discovery.
- TCP endpoint must not be used by business code as the primary destination identity.
- topology rules such as "who dials whom" must not appear in application APIs.

## Core Identity And Object Model

This section is the main output of step 2.

### ServiceType

`ServiceType` identifies a logical service kind, not a running process.

Examples:

- `gate`
- `game`
- `relay`
- `match`

Suggested representation in C++:

```cpp
using ServiceType = std::uint32_t;
```

Rules:

- Stable across deployments
- Assigned by configuration or generated code, not by runtime discovery
- Used for routing policy, broadcast scope, and receiver resolution

### InstanceId

`InstanceId` identifies one logical slot inside a service type.

Suggested representation:

```cpp
using InstanceId = std::uint32_t;
```

Rules:

- Unique within one `ServiceType`
- Not enough by itself to identify a running process after restart
- Can be allocated from discovery or from deployment configuration

### IncarnationId

`IncarnationId` distinguishes restarts of the same logical process slot.

Suggested representation:

```cpp
using IncarnationId = std::uint64_t;
```

Rules:

- Must change every time a process restarts
- Can be derived from startup timestamp, monotonic sequence, or discovery lease-bound token
- Used to reject stale routes and stale receiver ownership

### ProcessId

`ProcessId` identifies a logical process slot.

Suggested representation:

```cpp
struct ProcessId {
    ServiceType service_type;
    InstanceId instance_id;
};
```

Rules:

- Unique within one cluster namespace
- Stable across short-lived reconnects if the same process instance is still alive
- Must be paired with `IncarnationId` whenever stale-vs-current matters

### ProcessRef

`ProcessRef` identifies one concrete running process instance.

Suggested representation:

```cpp
struct ProcessRef {
    ProcessId process_id;
    IncarnationId incarnation_id;
};
```

Use `ProcessRef` when:

- validating receiver ownership
- validating direct links
- detecting stale routing information

Use only `ProcessId` when:

- expressing a stable logical target
- defining broadcast filters
- defining service-level policy

### ProcessDescriptor

`ProcessDescriptor` is the discovery-visible metadata for one running process.

Suggested representation:

```cpp
struct Endpoint {
    std::string host;
    std::uint16_t port;
};

struct ProcessDescriptor {
    ProcessRef process;
    std::string service_name;
    Endpoint listen_endpoint;
    std::uint32_t protocol_version;
    std::uint64_t start_time_unix_ms;
    std::map<std::string, std::string> labels;
    std::vector<ServiceType> relay_capabilities;
};
```

Required fields:

- process identity
- transport endpoint
- protocol compatibility marker

Optional fields:

- labels for environment, shard, zone, role
- relay capability metadata
- load hints later if needed

Rules:

- Discovery owns publication of the descriptor.
- Routing and receiver resolution may consume it.
- Application code should rarely need the raw descriptor.

### ReceiverType

`ReceiverType` identifies the semantic kind of a receiver.

Suggested representation:

```cpp
enum class ReceiverType : std::uint16_t {
    Process = 1,
    Player = 2,
    System = 3,
    Service = 4,
    Group = 5
};
```

Initial semantics:

- `Process`: target is a specific process
- `Player`: target is a player receiver
- `System`: target is a named or keyed subsystem
- `Service`: target is a logical service endpoint, with concrete instance chosen by routing
- `Group`: target expands to multiple receivers or processes

### ReceiverAddress

`ReceiverAddress` is the stable upper-layer target abstraction.

Suggested representation:

```cpp
struct ReceiverAddress {
    ReceiverType type;
    std::uint64_t key_hi;
    std::uint64_t key_lo;
};
```

Encoding examples:

- `Process`: `key_hi = service_type`, `key_lo = instance_id`
- `Player`: `key_lo = player_id`
- `System`: hashed system key
- `Service`: `key_lo = service_type`
- `Group`: group identifier

Rules:

- Application code should prefer `ReceiverAddress` over raw process routing whenever possible.
- The internal communication foundation owns the mapping from receiver to process.
- A receiver may temporarily resolve to:
  - local process
  - one remote process
  - multiple remote processes
  - unresolved

### ReceiverLocation

`ReceiverLocation` is the result of resolving a receiver address.

Suggested representation:

```cpp
enum class ReceiverLocationKind : std::uint8_t {
    Local,
    SingleProcess,
    MultiProcess,
    Unresolved
};

struct ReceiverLocation {
    ReceiverLocationKind kind;
    std::vector<ProcessRef> processes;
};
```

Rules:

- `Process` receivers usually resolve directly.
- `Player` receivers usually resolve to one process.
- `Group` receivers may resolve to many processes.

### BroadcastScope

`BroadcastScope` defines how a broadcast is expanded.

Suggested representation:

```cpp
struct BroadcastScope {
    std::optional<ServiceType> service_type;
    std::map<std::string, std::string> required_labels;
    bool include_local = true;
};
```

Examples:

- all `game` processes
- all `gate` processes in one zone
- all relay nodes

Rules:

- Broadcast is a routing concern, not an application-managed peer iteration concern.
- Application code must not manually enumerate discovered peers for routine broadcast.

### Envelope

`Envelope` is the internal data-plane wrapper for application business payloads.

Suggested representation:

```cpp
enum class DeliverySemantic : std::uint8_t {
    Direct = 1,
    Broadcast = 2
};

struct EnvelopeHeader {
    ProcessRef source_process;
    DeliverySemantic semantic;
    ReceiverAddress target_receiver;
    std::uint64_t request_id;
    std::uint32_t flags;
};

struct Envelope {
    EnvelopeHeader header;
    std::string payload_type_url;
    std::string payload_bytes;
};
```

Rules:

- Internal envelope is separate from external client protocol packets.
- `payload_type_url` must allow deterministic protobuf dispatch.
- Envelope must not embed transport-specific identifiers.

### Route

`Route` is the routing layer result for one outbound envelope.

Suggested representation:

```cpp
enum class RouteKind : std::uint8_t {
    Local,
    DirectLink,
    RelayHop,
    BroadcastFanout,
    Drop
};

struct Route {
    RouteKind kind;
    std::vector<ProcessRef> next_hops;
};
```

Rules:

- Route output is internal to the messaging stack.
- Upper application code must never branch on direct-vs-relay behavior.

## Identity Usage Rules

These rules are critical and should remain stable.

1. `ProcessId` is the stable logical identity.
2. `ProcessRef` is the concrete running identity.
3. `ReceiverAddress` is the preferred application-facing target.
4. `ConnectionId` is transport-only and never leaves the lower layers.
5. `Endpoint` is connectivity metadata, not business identity.
6. Stale state detection must rely on `IncarnationId`, not on endpoint comparison.

## Example Flows

### Send To A Specific Process

1. Application calls `SendToProcess(ProcessId, protobuf message)`.
2. Messaging facade converts the process target into a `ReceiverAddress` of type `Process`, or routes directly via process semantics.
3. Routing resolves local, direct link, or relay hop.
4. Link layer delivers over one or more transport connections.

### Send To A Player Receiver

1. Application calls `SendToReceiver(PlayerReceiver(player_id), protobuf message)`.
2. Receiver directory resolves player ownership to a `ProcessRef`.
3. Routing decides local, direct, or relay.
4. Messaging facade sends the internal envelope without exposing network details to business code.

### Broadcast To All Game Processes

1. Application calls `BroadcastToService(service_type: game, scope, protobuf message)`.
2. Routing expands the scope using membership and topology policy.
3. Messaging layer performs fanout.

## Phase Guidance

The first implementation should intentionally stay narrow.

Must include:

- process identity model
- process descriptor model
- receiver address model
- envelope model
- relay-first routing baseline
- direct-connect extension point

Should wait until later:

- advanced group semantics
- sophisticated load-aware routing
- multi-region discovery federation

## Open Questions

These are intentionally deferred to later design steps.

- How should `InstanceId` be assigned: deployment config, discovery allocation, or hybrid?
- Should `ServiceType` be numeric-only, string-only, or dual-mapped?
- Should `ReceiverAddress` support richer typed payloads instead of the `key_hi/key_lo` form?
- How should relay capability be advertised and selected?
- What consistency guarantees are required for receiver ownership changes?

## Internal Protocol Model

This section is the main output of step 3.

The internal protocol model is split into two independent protobuf systems:

- control-plane protocol
- data-plane protocol

Both are internal-only and must remain independent from any external client-facing protocol.

### Protocol Split

#### Control-Plane Protocol

Purpose:

- build and validate process links
- advertise process metadata
- exchange liveness and routing-related state
- manage receiver ownership metadata later when needed

Typical messages:

- hello
- hello_ack
- ping
- pong
- close
- route_update
- receiver_bind
- receiver_unbind

#### Data-Plane Protocol

Purpose:

- carry application business protobuf payloads between processes
- provide a uniform internal envelope
- support direct send, relay hop, and broadcast semantics

Typical content:

- source process
- destination receiver
- payload metadata
- message payload bytes

### Why The Split Is Mandatory

If control-plane and data-plane messages share one undifferentiated protocol namespace, these problems appear quickly:

- control logic becomes coupled to business dispatch
- protocol evolution becomes harder
- relay and direct-connect paths become less clear
- receiver ownership sync gets mixed with business payload transport

The split keeps behavior understandable:

- control plane changes topology or metadata
- data plane carries business messages

## Wire Framing

The transport layer should expose framed byte messages to the link layer.

Recommended wire frame:

```text
+----------------+----------------+----------------+-------------------+
| Magic (u32)    | Version (u16)  | Kind (u16)     | Length (u32)      |
+----------------+----------------+----------------+-------------------+
| Payload bytes...                                                   |
+--------------------------------------------------------------------+
```

Fields:

- `Magic`: identifies the internal IPC protocol family
- `Version`: wire framing version, not business payload version
- `Kind`: distinguishes control-plane protobuf from data-plane protobuf
- `Length`: payload size in bytes

Implementation rule:

- this fixed-size wire frame header may be represented as a native C++ struct inside the transport implementation
- this exception applies only to the tiny framing header
- control-plane and data-plane payload schemas must use protobuf

Initial kinds:

- `1`: control-plane message
- `2`: data-plane message

Rules:

- framing version changes only when the binary frame format changes
- protobuf schema evolution inside the payload must not require a framing version bump
- transport owns frame parsing, link layer owns payload decoding

## Control-Plane Protocol

### Control-Plane Design Rules

- All control-plane messages must use a dedicated protobuf package.
- Control-plane message handlers must live in the link, discovery, or routing parts of the stack.
- Application business code must never handle raw control-plane messages.
- Control-plane messages may change routing or receiver metadata, but must not carry business payloads.

### Suggested Protobuf Package

```proto
package some_server.ipc.control.v1;
```

### Envelope Strategy

Use one top-level protobuf wrapper for control-plane dispatch.

Suggested shape:

```proto
message ControlMessage {
  uint64 sequence = 1;
  oneof body {
    Hello hello = 10;
    HelloAck hello_ack = 11;
    Ping ping = 12;
    Pong pong = 13;
    Close close = 14;
    RouteUpdate route_update = 20;
    ReceiverBind receiver_bind = 30;
    ReceiverUnbind receiver_unbind = 31;
  }
}
```

Rationale:

- one decode entry point
- explicit control-plane message namespace
- easy validation in the link layer

### Hello / HelloAck

These messages establish a process link and validate compatibility.

Suggested shape:

```proto
message ProcessIdentity {
  uint32 service_type = 1;
  uint32 instance_id = 2;
  uint64 incarnation_id = 3;
}

message Endpoint {
  string host = 1;
  uint32 port = 2;
}

message Hello {
  ProcessIdentity self = 1;
  string service_name = 2;
  uint32 protocol_version = 3;
  uint32 min_supported_protocol_version = 4;
  Endpoint listen_endpoint = 5;
  map<string, string> labels = 6;
  repeated uint32 relay_capabilities = 7;
}

message HelloAck {
  enum Result {
    RESULT_UNSPECIFIED = 0;
    RESULT_OK = 1;
    RESULT_INCOMPATIBLE_VERSION = 2;
    RESULT_DUPLICATE_PROCESS = 3;
    RESULT_REJECTED = 4;
  }

  Result result = 1;
  ProcessIdentity self = 2;
  uint32 protocol_version = 3;
  string reason = 4;
}
```

Rules:

- `Hello` is the first semantic message on a new link.
- `HelloAck` confirms acceptance or explains rejection.
- `incarnation_id` is mandatory for stale link rejection.
- if compatibility fails, the link must be closed cleanly.

### Ping / Pong

Purpose:

- liveness
- RTT measurement if needed
- dead peer detection above plain TCP failure detection

Suggested shape:

```proto
message Ping {
  uint64 nonce = 1;
  uint64 send_time_unix_ms = 2;
}

message Pong {
  uint64 nonce = 1;
  uint64 send_time_unix_ms = 2;
}
```

### Close

Purpose:

- explicit semantic shutdown of a process link
- diagnostic visibility

Suggested shape:

```proto
message Close {
  uint32 code = 1;
  string reason = 2;
}
```

### RouteUpdate

This message is optional in the first implementation, but the protocol should reserve space for it now.

Purpose:

- distribute route hints
- advertise relay capabilities
- propagate topology changes later if routing becomes partially explicit

Suggested shape:

```proto
message RouteUpdate {
  repeated RouteEntry entries = 1;
}

message RouteEntry {
  uint32 service_type = 1;
  ProcessIdentity next_hop = 2;
  uint32 metric = 3;
}
```

### ReceiverBind / ReceiverUnbind

These messages are not required for the first runnable version if receiver ownership stays local-only or discovery-backed, but they should be part of the protocol plan.

Suggested shape:

```proto
message ReceiverAddress {
  uint32 type = 1;
  uint64 key_hi = 2;
  uint64 key_lo = 3;
}

message ReceiverBind {
  ReceiverAddress receiver = 1;
  ProcessIdentity owner = 2;
  uint64 version = 3;
}

message ReceiverUnbind {
  ReceiverAddress receiver = 1;
  ProcessIdentity owner = 2;
  uint64 version = 3;
}
```

`version` here is a receiver ownership version, not a process protocol version.

## Data-Plane Protocol

### Data-Plane Design Rules

- Data-plane protobuf must be independent from control-plane protobuf.
- Data-plane messages must always travel inside a stable internal envelope.
- Business payload bytes must be opaque to routing and link layers.
- External client packet formats must not be tunneled directly as the internal envelope.

### Suggested Protobuf Package

```proto
package some_server.ipc.data.v1;
```

### Envelope Model

Suggested shape:

```proto
message ProcessRef {
  uint32 service_type = 1;
  uint32 instance_id = 2;
  uint64 incarnation_id = 3;
}

message ReceiverAddress {
  uint32 type = 1;
  uint64 key_hi = 2;
  uint64 key_lo = 3;
}

message BroadcastScope {
  optional uint32 service_type = 1;
  map<string, string> required_labels = 2;
  bool include_local = 3;
}

message DataEnvelope {
  enum DeliverySemantic {
    DELIVERY_SEMANTIC_UNSPECIFIED = 0;
    DELIVERY_SEMANTIC_DIRECT = 1;
    DELIVERY_SEMANTIC_BROADCAST = 2;
  }

  ProcessRef source_process = 1;
  DeliverySemantic semantic = 2;
  ReceiverAddress target_receiver = 3;
  optional BroadcastScope broadcast_scope = 4;
  string payload_type_url = 5;
  bytes payload_bytes = 6;
  uint64 request_id = 7;
  uint32 flags = 8;
}
```

Rules:

- `payload_type_url` is the protobuf dispatch key
- `payload_bytes` stores serialized business protobuf
- `target_receiver` is always present
- `broadcast_scope` is set only for broadcast semantics
- `source_process` always includes `incarnation_id`

### Relay Header Consideration

For the first phase, relay metadata should remain part of the envelope-processing context, not exposed as a separate application-visible header.

If needed later, extend with:

```proto
message TransitMetadata {
  uint32 hop_count = 1;
  repeated ProcessRef visited_relays = 2;
}
```

This must stay internal to the routing/data-plane implementation.

## Payload Dispatch Strategy

Business payload dispatch should use `payload_type_url`, not transport protocol ids or ad hoc integer enums owned by each application.

Recommended approach:

- protobuf payload types remain application-owned
- messaging facade maintains a registry:
  - type URL -> protobuf factory
  - type URL -> handler

This gives:

- deterministic dispatch
- fewer global integer collisions
- easier protocol evolution

## Versioning Strategy

There are three different versions and they must not be mixed.

### 1. Wire Framing Version

Scope:

- raw frame layout only

Changes when:

- binary frame header changes
- framing semantics change

Does not change when:

- protobuf fields are added compatibly

### 2. Internal Protocol Version

Scope:

- control-plane and data-plane protobuf compatibility contract

Represented by:

- `protocol_version`
- `min_supported_protocol_version`

Used during:

- hello / hello_ack handshake

Rules:

- a process must reject a peer whose protocol version is lower than its minimum supported version
- a process may also reject a peer if its own version is lower than the peer's minimum supported version
- this version should change only for intentionally incompatible internal protocol changes

### 3. Business Payload Schema Evolution

Scope:

- application-owned protobuf payload messages inside `payload_bytes`

Rules:

- handled through protobuf-compatible schema evolution
- should not require a transport or framing version bump
- should not require changing control-plane protocol unless routing semantics change

## Compatibility Rules

These rules should remain stable.

1. Additive protobuf field changes are the default path.
2. Required semantic changes must trigger an internal protocol version decision.
3. Framing changes are rare and must be treated as exceptional.
4. Unknown control-plane messages must be logged and rejected safely.
5. Unknown data-plane payload types must be rejected at dispatch time without crashing the process.
6. Stale messages must be rejected using `ProcessRef.incarnation_id` when ownership or source identity matters.

## Error Handling Rules

### Control Plane

- link establishment failures must be explicit in logs
- version mismatch should return `HelloAck.RESULT_INCOMPATIBLE_VERSION`
- duplicate live process identity should return `HelloAck.RESULT_DUPLICATE_PROCESS`
- invalid or malformed control messages should close the link

### Data Plane

- unknown payload type should produce a structured error log and drop the message
- unresolved receiver should be dropped or dead-lettered according to later policy
- route resolution failure must not surface as transport-layer corruption

## Initial Protobuf File Layout

Recommended repository layout once protocol implementation starts:

```text
proto/ipc/control/v1/control.proto
proto/ipc/data/v1/envelope.proto
proto/ipc/common/v1/types.proto
```

Recommended split:

- `common/v1/types.proto`
  - `ProcessIdentity`
  - `ProcessRef`
  - `Endpoint`
  - `ReceiverAddress`
  - `BroadcastScope`
- `control/v1/control.proto`
  - control-plane wrapper and control-plane messages
- `data/v1/envelope.proto`
  - data-plane envelope

## Initial Implementation Guidance

The first implementation should only require this subset:

- wire frame with control/data kind split
- `Hello`
- `HelloAck`
- `Ping`
- `Pong`
- `Close`
- `DataEnvelope`

These can wait:

- `RouteUpdate`
- `ReceiverBind`
- `ReceiverUnbind`
- transit metadata

## Discovery And Membership Model

This section is the main output of step 4.

The current preferred discovery backend is `etcd`, but this section describes the storage model as an architectural contract, not as leaked business logic.

Discovery has one job:

- publish and observe live process membership

Discovery must not:

- decide routing
- decide direct-connect topology
- decide receiver ownership policy

## Discovery Responsibilities

The discovery layer is responsible for:

- publishing the local `ProcessDescriptor`
- keeping the registration alive
- removing registration on graceful shutdown when possible
- observing add/update/remove membership events
- exposing a current membership snapshot

The discovery layer is not responsible for:

- proactive peer dial decisions
- route computation
- relay selection
- business-level service balancing

## Discovery Namespace Model

All records live under one cluster namespace.

Suggested namespace dimensions:

- environment
- cluster name
- protocol family

Suggested conceptual form:

```text
/some_server/ipc/{environment}/{cluster}/...
```

Examples:

- `/some_server/ipc/dev/local/...`
- `/some_server/ipc/test/cluster-a/...`
- `/some_server/ipc/prod/live-1/...`

Rules:

- one runtime cluster must not share a discovery namespace with another cluster
- internal IPC state must be isolated from unrelated application configuration state
- changing namespace is a deployment concern, not a business code concern

## Key Layout

The first version should keep the key layout minimal.

Suggested layout:

```text
/some_server/ipc/{env}/{cluster}/members/{service_type}/{instance_id}
/some_server/ipc/{env}/{cluster}/leases/{lease_id}
/some_server/ipc/{env}/{cluster}/alloc/service/{service_type}/next_instance_id
```

### Members Key

Primary membership record:

```text
/some_server/ipc/{env}/{cluster}/members/{service_type}/{instance_id}
```

Value:

- serialized `ProcessDescriptor`

Attached lease:

- discovery lease owned by the live process

Why this key shape:

- easy prefix watch by cluster
- easy prefix watch by service type
- stable logical identity per process slot

### Leases Key

Optional diagnostic record:

```text
/some_server/ipc/{env}/{cluster}/leases/{lease_id}
```

Value may contain:

- `ProcessRef`
- registration timestamp
- debug-only metadata

This key is optional for the first runnable version.

Purpose:

- operational debugging
- easier investigation of orphaned or conflicting records

### Allocation Key

Optional key for dynamic `InstanceId` allocation:

```text
/some_server/ipc/{env}/{cluster}/alloc/service/{service_type}/next_instance_id
```

This is only needed if the system chooses discovery-backed allocation.

If instance ids come from deployment config, this key can be omitted.

## Registration Record

The member value should be the serialized `ProcessDescriptor`.

Suggested required fields:

- `process.process_id.service_type`
- `process.process_id.instance_id`
- `process.incarnation_id`
- `service_name`
- `listen_endpoint`
- `protocol_version`
- `start_time_unix_ms`

Suggested optional fields:

- `labels`
- `relay_capabilities`

### Registration Invariants

These invariants are important:

1. One members key represents one logical `ProcessId`.
2. One live process owns at most one members key.
3. One members key must contain exactly one current `IncarnationId`.
4. Record liveness is lease-bound, not inferred from endpoint reachability.
5. Consumers must treat `ProcessRef` as the concrete identity, not just the key path.

## Lease Ownership Model

Each live process owns one discovery lease.

Recommended model:

- create lease during discovery startup
- write the member record attached to that lease
- keep the lease alive until process shutdown
- revoke lease on graceful shutdown if possible

### Why Lease Ownership Is Important

Without lease-bound registration:

- crashed processes leave stale membership
- topology convergence becomes slow and ambiguous
- routing may keep resolving to dead instances

The lease is the primary liveness source.

### One Lease Per Process

The first implementation should use:

- one lease per process
- one primary member record attached to that lease

Avoid in the first phase:

- multiple member records per lease
- receiver ownership records on the same lease unless clearly justified

That keeps cleanup semantics obvious.

## Registration Algorithm

The registration sequence should be:

1. Build local `ProcessDescriptor`
2. Create lease
3. Attempt atomic write of member key
4. Start keepalive
5. Publish local membership-ready event to upper layers

### Atomic Write Requirement

Registration must use a compare-and-swap style transaction.

Required behavior:

- create key if absent
- if key exists with same `IncarnationId`, treat as retry or reconnect case
- if key exists with different `IncarnationId`, treat as conflict and resolve explicitly

This prevents silent takeover of a logical process slot.

### Conflict Cases

Possible cases:

1. Previous process is actually dead and lease already expired
2. Previous process is still live
3. Current process restarted before old membership fully disappeared

Recommended first-phase behavior:

- if a conflicting live key exists, fail startup of the new process
- do not silently overwrite

This is the simplest and safest behavior.

## Membership Snapshot And Watch Semantics

The discovery layer must provide two views:

- snapshot
- incremental events

### Snapshot

Snapshot is a point-in-time list of currently visible `ProcessDescriptor` records.

Consumers:

- routing
- receiver directory
- observability tooling later

Rules:

- snapshot is used for initialization
- first-phase correctness may rely on a fresh-snapshot reconciliation model
  instead of strict revision-coupled watch startup
- snapshot must contain `ProcessRef`, not only `ProcessId`

### Watch

Watch emits membership changes after snapshot initialization.

Suggested event model:

```cpp
enum class MembershipEventType : std::uint8_t {
    Added,
    Updated,
    Removed
};

struct MembershipEvent {
    MembershipEventType type;
    ProcessDescriptor process;
};
```

Interpretation:

- `Added`: first visible live record for this `ProcessRef`
- `Updated`: same `ProcessId`, same `IncarnationId`, descriptor changed
- `Removed`: record disappeared or lease expired

### Snapshot + Watch Contract

First-phase discovery consumers should initialize as:

1. obtain a fresh snapshot
2. apply the snapshot locally
3. start a cluster-wide watch loop
4. reconcile membership again after watch startup if needed
5. apply incremental changes by refreshing from watch-triggered events

This gives a compensating eventually consistent model that is simpler than
full revision-coupled startup and is acceptable for the first phase.

Later phases may strengthen this to a strict revision-based contract:

1. obtain snapshot at revision `R`
2. apply snapshot
3. start watch from revision `R + 1`
4. apply incremental events

## Membership Change Semantics

Membership is about process presence, not business readiness.

Meaning of membership states:

- present in discovery: process is alive enough to hold lease and publish descriptor
- absent from discovery: process must be treated as unavailable

Membership does not guarantee:

- process is fully warmed up for all business traffic
- receiver ownership is fully synchronized
- direct link already exists

If readiness beyond liveness is needed later, add explicit readiness metadata to the descriptor or control plane.

Do not overload membership presence with too many meanings in the first phase.

## Service-Level Views

Consumers often need a subset of the full membership set.

Discovery should support efficient logical queries for:

- all members in cluster
- all members of one `ServiceType`
- one exact `ProcessId`

These are view concerns only.

They must not require new storage schemas beyond the primary members key layout.

## InstanceId Allocation Strategies

This was left open in step 2. Step 4 narrows the options.

### Option A: Deployment-Assigned InstanceId

Each process starts with an explicit configured `InstanceId`.

Pros:

- simplest semantics
- stable identities
- easy debugging

Cons:

- deployment system must manage uniqueness

### Option B: Discovery-Allocated InstanceId

The process obtains `InstanceId` from discovery using an allocation key or transaction.

Pros:

- less deployment coordination

Cons:

- more discovery complexity
- harder to reason about stable logical identity
- trickier restart semantics

### Recommended First-Phase Choice

Use `deployment-assigned InstanceId`.

Reason:

- fewer moving parts
- clearer conflicts
- better fit for a staged design

This follows KISS and keeps discovery focused on membership, not identity generation.

## Stale Record Handling

Stale records are unavoidable during process crashes, network partitions, or quick restart races.

The design must define how they are recognized and what layer owns cleanup.

### Primary Stale Record Rule

If a record still has a valid live lease, it is not stale from discovery's point of view.

If the lease expires, the member record must disappear automatically.

### Stale Descriptor Rule

Even if a consumer cached old process metadata, `IncarnationId` is the authority for freshness.

Consumers must:

- reject cached ownership or routes tied to an older `IncarnationId`
- refresh state when discovery shows a new `ProcessRef`

### Fast Restart Race

Example:

1. process A with `ProcessId(game, 3)` and `IncarnationId=100` is running
2. process A dies
3. process B restarts quickly with same `ProcessId(game, 3)` and `IncarnationId=101`
4. some consumers still hold routes or receiver ownership pointing to incarnation `100`

Correct behavior:

- discovery eventually removes incarnation `100`
- new registration publishes incarnation `101`
- consumers treat `101` as a new concrete process
- old links, routes, and receiver ownership referencing `100` are invalid

### Manual Cleanup

The first implementation should avoid depending on manual cleanup.

Manual cleanup may exist as an operational tool, but correctness must not depend on it.

## Network Partition Semantics

Discovery behavior during a partition must be defined conservatively.

If a process cannot keep its lease alive:

- its registration must expire
- it must be treated as absent by other members

If the isolated process is still running locally:

- it must not assume it is still a valid cluster member forever
- upper layers should eventually treat discovery loss as a fatal membership condition or enter a degraded state

Recommended first-phase behavior:

- losing discovery lease is fatal for cluster membership
- the process should stop participating in inter-process messaging once lease ownership is lost

This is strict, but much easier to reason about than partial split-brain handling.

## Consumer Rules

Routing and receiver consumers of discovery must follow these rules:

1. Never key caches only by endpoint.
2. Always track `ProcessRef`, not just `ProcessId`.
3. Treat membership removal as authoritative unavailability.
4. Treat membership add with new `IncarnationId` as a new concrete process.
5. Never infer message reachability directly from discovery presence alone.

Rule 5 matters because:

- discovery says who exists
- routing decides how to reach them
- link decides whether a current direct path exists

## Recommended etcd Watch Scope

Recommended first-phase watch granularity:

- cluster-wide watch for all members

Optional later optimization:

- service-type-specific watches

Reason:

- one cluster-wide watch is simpler
- routing can derive service-specific views in memory
- avoids premature optimization and duplicated watch logic

## Discovery Failure Handling

### Startup Failure

If discovery cannot:

- connect to backend
- create lease
- register member record

Then startup should fail.

This is the safest default for a system that depends on inter-process communication.

### Runtime Failure

If keepalive fails transiently:

- discovery should retry within lease safety limits

If lease ownership is lost:

- emit local fatal membership loss
- upper layers should stop routing outbound inter-process traffic

Current first-phase implementation may satisfy this by entering a local
degraded membership state instead of immediately terminating the process.

Required degraded-state behavior:

- `registered` becomes false
- `ipc_ready` becomes false
- business-facing IPC send paths reject new sends
- forwarding and auto-connect stop participating in inter-process messaging

### Watch Failure

If watch stream breaks:

- first-phase implementation may rebuild from a fresh snapshot and restart watch
- later phases may re-establish watch from a safe revision when backend support is added

Correctness is more important than event continuity elegance in the first phase.

## Initial etcd Transaction Guidance

The registration transaction should enforce:

- expected key absence for a new process slot
- or expected same incarnation for idempotent retry

The exact etcd transaction form can be decided in implementation, but the design intent is fixed:

- no blind overwrite
- no silent dual ownership

## First-Phase Implementation Boundary

The first implementation should include:

- member record registration with lease
- keepalive
- graceful revoke on shutdown
- snapshot + watch
- conflict detection on duplicate logical process slots
- stale detection via `IncarnationId`

The first implementation should not require:

- dynamic instance allocation
- rich readiness state machine
- separate receiver discovery records
- multi-backend federation

## Topology And Route Resolution

This section is the main output of step 5.

The topology model must satisfy two requirements at the same time:

- keep the first implementation minimal and predictable
- preserve a clean upgrade path from relay-first delivery to direct-connect optimization

The chosen strategy is:

- first-phase baseline: `relay-first`
- later optimization path: `direct-upgrade`
- stable upper-layer API regardless of physical path

## Routing Layer Responsibilities

The routing layer is responsible for:

- accepting a fully formed outbound message intent
- resolving the target into one or more next hops
- selecting local delivery, direct delivery, relay delivery, or broadcast fanout
- hiding topology choices from upper layers

The routing layer is not responsible for:

- discovery registration
- TCP connection management
- link handshake
- business payload handling
- receiver ownership lifecycle itself

The routing layer is the only layer that should answer:

- where should this message go next

It must not answer:

- who is alive in the cluster
- how a TCP connection is created
- what the business payload means

## Routing Inputs And Outputs

### Inputs

Routing consumes stable read-only inputs:

- local `ProcessRef`
- outbound `Envelope`
- optional `ReceiverLocation`
- `MembershipView`
- `LinkView`
- `TopologyPolicy`

Suggested context:

```cpp
struct RoutingContext {
    ProcessRef self;
    Envelope envelope;
    std::optional<ReceiverLocation> receiver_location;
    const MembershipView& membership;
    const LinkView& links;
};
```

### Output

Routing should emit an executable route plan rather than a single enum.

Suggested representation:

```cpp
enum class RoutePlanKind : std::uint8_t {
    LocalDelivery,
    SingleNextHop,
    MultiNextHop,
    Unreachable,
    Drop
};

struct RouteHop {
    ProcessRef next_hop;
    bool direct;
};

struct RoutePlan {
    RoutePlanKind kind;
    std::vector<RouteHop> hops;
};
```

Why this shape:

- supports relay and broadcast naturally
- does not leak transport-level connection ids
- can evolve without changing application-facing APIs

## Logical Topology vs Physical Topology

The design must keep these separate.

Logical topology:

- application sends from one process to a process or receiver target

Physical topology:

- local dispatch
- direct process-to-process link
- relay-mediated forwarding
- fanout to multiple next hops

Applications operate on logical topology only.

Routing owns physical-path selection.

## First-Phase Topology Baseline

The first implementation should use these rules:

1. Every ordinary business process must be able to reach at least one relay process.
2. Ordinary business processes are not required to maintain direct links to every other process.
3. Existing healthy direct links may be used when available.
4. If no direct path is already available, relay is the default remote path.
5. If no relay path exists, the route is unreachable.

This gives a predictable baseline without forcing full mesh complexity.

### First-Phase Auto-Connect Behavior

The first phase may use a small amount of application-level topology glue to
reduce manual runtime operations.

Current first-phase behavior:

- `relay` automatically connects to discovered `game` members
- `game` automatically connects to discovered `relay` members
- startup includes a snapshot-based reconcile pass so convergence does not
  depend only on future watch events
- if no healthy relay link exists, `game` may perform a bounded
  refresh-and-reconcile step before retrying auto-connect

Rules:

- this logic lives in thin application IPC services, not in the routing API
- discovery still only answers membership
- routing still only answers next-hop selection
- link still only manages connection and handshake state
- first-phase auto-connect is policy glue, not a general cluster-autonomy system

This keeps the relay-first topology practical without introducing a separate
topology-management subsystem too early.

## Route Resolution Order

The first-phase route resolution order should be fixed:

1. local delivery
2. existing healthy direct link
3. relay delivery
4. optional direct-upgrade suggestion
5. unreachable or drop

The route resolution order must be stable and must not depend on ad hoc application logic.

## Topology Policy Interface

Topology policy should be encapsulated behind a strategy interface.

Suggested interface:

```cpp
class ITopologyPolicy {
public:
    virtual RoutePlan Resolve(const RoutingContext& ctx) const = 0;
};
```

Planned implementations:

- `RelayFirstPolicy`
- `HybridPolicy`

### First-Phase Choice

The first implementation should only require:

- `RelayFirstPolicy`

`HybridPolicy` should remain a design target, not an immediate implementation requirement.

This keeps the first release simple while preserving a clean evolution path.

## Routing Views

Routing must not depend directly on discovery internals or link internals.

Suggested read-only views:

```cpp
class MembershipView {
public:
    virtual std::optional<ProcessDescriptor> Find(ProcessId id) const = 0;
    virtual std::vector<ProcessDescriptor> FindByService(ServiceType type) const = 0;
    virtual std::vector<ProcessDescriptor> All() const = 0;
};

class LinkView {
public:
    virtual bool HasHealthyDirectLink(ProcessRef target) const = 0;
    virtual std::vector<ProcessRef> AvailableRelays() const = 0;
};
```

Rules:

- membership presence does not imply direct reachability
- routing must not inspect backend discovery records directly
- routing must not inspect raw transport connections directly

## Target Resolution Model

Before next-hop selection, the routing layer should operate on one of two semantic target forms:

- single-process target
- multi-process target

Target forms are produced as follows:

- `Process` receiver resolves directly to one process
- `Player` receiver typically resolves to one owning process
- `Service` receiver may resolve to one or more candidate processes
- `Group` receiver may resolve to many processes
- `BroadcastScope` expands to many processes

This keeps the routing problem narrow:

- single-target route selection
- multi-target fanout planning

## Four Route Classes

The first routing model should explicitly support four route classes.

### 1. Local

Conditions:

- target resolves to the local process

Result:

- `RoutePlanKind::LocalDelivery`

Behavior:

- bypass link and transport
- dispatch directly to local message handling

### 2. Direct

Conditions:

- target resolves to one remote process
- a healthy direct link already exists

Result:

- `RoutePlanKind::SingleNextHop`
- one hop with `direct = true`

Behavior:

- send directly to the target process

### 3. Relay

Conditions:

- target is remote
- no existing direct path is selected
- at least one relay is available

Result:

- `RoutePlanKind::SingleNextHop`
- one hop with `direct = false`

Behavior:

- send to the selected relay
- relay performs the next routing step or final fanout

### 4. Broadcast

Conditions:

- envelope semantic is broadcast

Result:

- `RoutePlanKind::MultiNextHop`

Behavior:

- expand scope into a process set
- produce local delivery and remote fanout plan

## Relay Role Definition

Relay is an infrastructure role, not a business application role.

Relay responsibilities:

- accept data-plane envelopes
- resolve next hop for remote delivery
- perform remote forwarding and may perform broadcast fanout in later phases
- optionally maintain routing-support metadata

Relay must not become:

- a business logic processor
- an external client protocol endpoint
- a general-purpose application state holder

Keeping relay narrow is essential for low coupling.

## Relay Selection

The first implementation should keep relay selection simple.

Suggested first-phase behavior:

- identify relay instances by service type
- choose one available healthy relay
- use a minimal selection rule such as:
  - first healthy
  - round robin

Do not require advanced scoring in the first phase.

This follows KISS and keeps routing deterministic.

## Direct-Upgrade Design

Direct delivery is an optimization path, not the baseline path.

Direct-upgrade means:

- the system may establish a direct process-to-process link for selected traffic patterns
- once the link exists and is healthy, routing may prefer it
- if the link disappears, routing falls back to relay automatically

### Critical Rule

Routing may suggest a direct link, but a normal send call must not synchronously block on direct link creation.

This rule must remain fixed.

Reason:

- blocking direct creation inside send would mix routing with connection lifecycle
- send latency would become unpredictable
- error handling would become much harder

Correct pattern:

1. current message uses relay
2. background mechanism decides whether to establish direct link
3. later messages may switch to direct route

## Suggested Direct-Upgrade Signals

These signals are allowed later, but they are not required in the first implementation:

- high traffic volume between one source and one target
- large message payload sizes
- latency-sensitive service pairs
- explicit policy allowlist for service-to-service direct links

The first implementation should only preserve extension points for these signals.

## Broadcast Resolution

Broadcast must remain a routing concern, not an application-side peer loop.

### First-Phase Supported Broadcast Scope

The first implementation should support:

- broadcast by `ServiceType`
- broadcast by label filter
- optional inclusion of local process

### Resolution Flow

1. expand `BroadcastScope` using `MembershipView`
2. filter candidates by labels and health view
3. separate local target from remote targets
4. generate local delivery if needed
5. generate remote fanout plan

### Fanout Strategy

For the first phase, remote broadcast fanout should use source-side expansion into
per-target sends while keeping fanout as a routing/messaging concern rather than an
application-managed peer loop.

Reason:

- keeps the first implementation small and easy to verify
- reuses the existing direct/relay single-target send path for each broadcast target
- preserves a clean upgrade path to relay-mediated broadcast fanout later if needed

## Route Failure Semantics

Route failures must be explicit and typed.

### Unreachable

Meaning:

- target is conceptually valid
- current topology has no usable path

Examples:

- no relay available
- no healthy direct link and no relay path

### Drop

Meaning:

- message should not be retried through normal routing

Examples:

- receiver resolution is definitively invalid
- incompatible protocol or policy rejection

These two outcomes must remain distinct because retry behavior and observability differ.

## First-Phase Minimal Routing Implementation

The minimal acceptable implementation for phase 1 is:

- implement `RelayFirstPolicy`
- support local delivery
- support direct delivery only when a healthy direct link already exists
- support relay delivery for remote targets
- support broadcast planning
- expose direct-upgrade as a future extension point only

This is the intended first deliverable, and it should not expand into full mesh sophistication prematurely.

## Global Consistency Rules

These rules should remain stable:

1. Discovery says which processes are present.
2. Receiver directory says where a receiver currently lives.
3. Routing says how to reach the target.
4. Link says whether a direct path is currently healthy.
5. Transport says how bytes move over a specific connection.

If any layer starts answering another layer's question, coupling will rise quickly.

## Receiver Directory And Messaging Facade

This section is the main output of step 6.

The goal of this layer is simple:

- application code should primarily send to receivers, not to transport connections or topology details

This layer must combine two things cleanly:

- receiver ownership resolution
- application-facing messaging APIs

## Receiver Directory Scope

The receiver directory answers one question:

- where does this receiver currently live

It is responsible for:

- resolving receiver ownership
- tracking local receiver ownership
- supporting bind, rebind, and invalidate operations
- providing receiver location information to routing

It is not responsible for:

- sending messages
- computing next-hop routes
- managing discovery membership
- maintaining transport links
- interpreting business payload meaning

This boundary is important. If receiver directory starts deciding delivery paths, it becomes a second routing layer.

## First-Phase Receiver Types

The first phase should fully support these receiver types:

- `Process`
- `Player`
- `Service`

The following remain part of the model but are not required as first-phase complete features:

- `System`
- `Group`

Why this scope:

- `Process` is the lowest stable target abstraction
- `Player` is the most important business receiver type
- `Service` is needed for service-level logical addressing

This keeps the first implementation useful without expanding into every future receiver shape.

## Receiver Semantics

### Process Receiver

Meaning:

- the target is one specific logical process

Resolution:

- directly to one `ProcessRef` or one concrete local process

### Player Receiver

Meaning:

- the target is one player logical receiver

Resolution:

- normally to exactly one authoritative owning process

Rule:

- one player receiver may have only one authoritative owner at a time

This rule is fixed for the first phase.

### Service Receiver

Meaning:

- the target is a logical service endpoint, not a specific process instance

Resolution:

- to one or more candidate service processes
- final instance selection is still a routing concern

Important distinction:

- `ServiceReceiver` is single-delivery intent
- `BroadcastScope(service_type=...)` is multi-delivery intent

These must never be conflated.

## Receiver Location Model

Step 2 introduced the basic location shape. Step 6 adds a receiver-ownership version.

Suggested model:

```cpp
enum class ReceiverLocationKind : std::uint8_t {
    Local,
    SingleProcess,
    MultiProcess,
    Unresolved
};

struct ReceiverLocation {
    ReceiverLocationKind kind;
    std::vector<ProcessRef> processes;
    std::uint64_t version;
};
```

### Receiver Ownership Version

`version` is a receiver ownership version.

It is used to:

- identify stale ownership cache entries
- validate migrations
- reject outdated relay or routing hints later if needed

It is not:

- process protocol version
- discovery record revision
- process incarnation id

## Ownership Consistency Model

The first-phase consistency target is intentionally conservative.

Rules:

1. One receiver has at most one authoritative owner at a time when its semantics require single ownership.
2. Receiver ownership may be temporarily observed stale by caches.
3. Newer ownership versions supersede older ownership versions.
4. `ProcessRef` plus receiver ownership `version` is enough to detect stale mappings in the first phase.

This is an eventually consistent ownership model with explicit stale detection, not a globally synchronous ownership model.

## Receiver Directory Backend Choice

### Chosen First-Phase Direction

The first phase will not use a remote shared receiver directory.

The chosen model is:

- no remote shared receiver-directory store
- hybrid resolution model

### Why Not Remote Shared Receiver Directory

A remote shared receiver directory can help when:

- many processes must independently resolve arbitrary receivers
- receiver ownership must be globally queryable by every process
- there is no stable relay or coordination role

Those benefits are not strong enough for the current design because:

- the system already chose relay-first as the baseline topology
- receiver ownership changes can be coordinated through relay and local owners
- adding a second high-churn distributed data set would raise complexity significantly

Costs of a shared receiver directory would include:

- more etcd write churn
- ownership conflict handling across all processes
- cache invalidation complexity everywhere
- another consistency surface in addition to discovery

For the current staged design, those costs outweigh the benefit.

## Hybrid Receiver Directory Model

The chosen receiver directory model is hybrid.

Meaning:

- each process maintains its local receiver directory for receivers it owns
- relay participates in cross-process receiver resolution support
- ordinary business processes do not require a fully replicated global receiver map

### Responsibilities In Hybrid Mode

Local process:

- knows receivers it currently owns
- binds and invalidates its local receiver entries
- can resolve its own local receivers directly

Relay:

- helps with cross-process receiver resolution
- supports relay-first message delivery for non-local receivers
- may cache receiver ownership information needed for forwarding

Ordinary business process:

- does not need complete global receiver ownership knowledge
- can ask messaging/routing to deliver without understanding full topology

This aligns with the relay-first baseline and keeps ordinary processes simpler.

## Receiver Directory Operations

The first-phase directory should expose four core operations.

### Resolve

Purpose:

- query where a receiver currently lives

Suggested shape:

```cpp
virtual ReceiverLocation Resolve(const ReceiverAddress& receiver) const = 0;
```

### Bind

Purpose:

- create an initial ownership mapping

Examples:

- player first enters a game process
- local service endpoint becomes active

### Rebind

Purpose:

- move ownership from one process to another

Examples:

- player migrates
- player reconnect logic lands on a new owner

### Invalidate

Purpose:

- remove or invalidate an ownership mapping

Examples:

- owner unloads receiver
- player leaves
- process shutdown invalidates local owned receivers

### Why Only These Four

These four operations are sufficient for the first phase.

Do not add early complexity such as:

- partial ownership patching
- group merge semantics
- multi-master ownership writes

That would exceed the current design stage.

## Receiver Ownership Rules

The following rules are fixed for the first phase.

1. A `PlayerReceiver` has only one authoritative owner at a time.
2. Ownership change must produce a newer receiver ownership `version`.
3. A process must invalidate local ownership records it can no longer serve.
4. Routing may temporarily observe stale ownership, but stale ownership must be detectable.
5. Ownership is logically separate from discovery membership.

Rule 5 matters because:

- discovery says whether a process exists
- receiver directory says whether that process owns a receiver

These are related but not interchangeable concepts.

## Receiver Directory And Discovery Relationship

Discovery and receiver directory may use the same broad infrastructure environment, but they are logically separate.

Discovery owns:

- process membership
- process liveness

Receiver directory owns:

- receiver ownership
- receiver location

In the first phase:

- receiver ownership is not stored as part of the discovery member record
- receiver ownership is not modeled as a second globally shared distributed data set

This is intentional.

## Application-Facing Messaging API

The first-phase messaging facade should remain minimal.

Suggested interface:

```cpp
class IMessenger {
public:
    virtual SendResult SendToProcess(
        ProcessId target,
        const google::protobuf::Message& message) = 0;

    virtual SendResult SendToReceiver(
        const ReceiverAddress& target,
        const google::protobuf::Message& message) = 0;

    virtual SendResult BroadcastToReceiver(
        const ReceiverAddress& target,
        const BroadcastScope& scope,
        const google::protobuf::Message& message) = 0;

    virtual SendResult BroadcastToService(
        ServiceType service_type,
        const BroadcastScope& scope,
        const google::protobuf::Message& message) = 0;
};
```

### API Principles

- `SendToProcess` exists for explicit process targeting
- `SendToReceiver` is the preferred application-facing abstraction
- broadcast remains receiver-oriented at the API boundary
- `BroadcastToService` is the first-phase convenience API

First-phase broadcast support:

- `BroadcastToReceiver(ServiceReceiver, ...)` is supported
- `BroadcastToService(...)` is supported
- broadcast to `ProcessReceiver`, `PlayerReceiver`, `SystemReceiver`, and `GroupReceiver` is reserved for later phases and should return a clear not-supported result

The first phase should not require:

- request/response API
- retry API
- future/promise-based RPC abstraction
- dead-letter queue API

Those can be layered later if needed.

## Local Receive-Side Dispatch

Inbound data-plane dispatch should be receiver-aware.

Recommended flow:

1. inbound `DataEnvelope` arrives
2. envelope metadata is validated
3. target receiver is resolved locally
4. payload type is decoded through the payload registry
5. local receiver host dispatches business handling

This keeps receiver semantics above raw payload dispatch.

## ReceiverHost Model

The first phase will use `ReceiverHost` as the local dispatch abstraction.

This is the chosen direction.

Meaning:

- a receiver host manages one class of receivers
- individual business objects do not all register themselves directly as global dispatch endpoints

Suggested interface:

```cpp
class IReceiverHost {
public:
    virtual bool CanHandle(ReceiverType type) const = 0;
    virtual DispatchResult Dispatch(
        const ReceiverAddress& target,
        const Envelope& envelope) = 0;
};
```

Examples:

- `PlayerManager` as a player receiver host
- `SystemManager` as a system receiver host
- `ServiceHost` as a service receiver host

### Why ReceiverHost Is Preferred

Benefits:

- avoids over-registering every individual object globally
- keeps object lifecycle management local
- fits manager-based server architecture naturally
- keeps dispatch structure simpler for large object counts

This is a better fit for the current project than a pure actor-style one-object-one-registration model.

## Payload Handler Registration

Receiver-aware dispatch still needs payload decoding by protobuf type.

Recommended split:

- messaging facade owns payload type registry
- receiver host owns target-specific business dispatch

Meaning:

1. payload type identifies how to decode the protobuf
2. receiver host identifies where inside the process the message should go

This avoids mixing protobuf decoding with receiver ownership.

## Local Dispatch Semantics

The first phase should support host-based local dispatch for:

- `PlayerReceiver`
- `ServiceReceiver`
- later `SystemReceiver`

Expected behavior:

- if local receiver host cannot find the target instance, dispatch returns a structured failure
- that failure is not transport corruption
- routing and messaging may choose to log or drop depending on message semantics later

## First-Phase Minimal Receiver Implementation

The minimal acceptable implementation for phase 1 is:

- full support for `Process`, `Player`, and `Service` receiver semantics
- receiver directory operations:
  - `Resolve`
  - `Bind`
  - `Rebind`
  - `Invalidate`
- receiver ownership `version`
- `ReceiverHost`-based local dispatch
- application-facing messaging API:
  - `SendToProcess`
  - `SendToReceiver`
  - `Broadcast`

The first phase should not require:

- globally shared remote receiver directory
- full `GroupReceiver` implementation
- synchronous request/response abstractions
- automatic retry semantics
- dead-letter infrastructure

## Global Boundary Rules

These rules should remain stable.

1. Discovery tracks process membership.
2. Receiver directory tracks receiver ownership.
3. Routing selects next hops.
4. Messaging facade exposes application APIs.
5. Receiver hosts perform local target dispatch.

If a layer starts absorbing another layer's role, the architecture will become harder to evolve.

## Recommended Next Step

Step 7 should define the first implementation slice and module layout:

- concrete first-phase module boundaries in this repository
- proto file layout
- framework service breakdown
- startup order
- first executable path for relay and one business service
