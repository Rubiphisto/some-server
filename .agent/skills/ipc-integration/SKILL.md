---
name: ipc-integration
description: Use when running or validating some-server real IPC integration scenarios that need etcd, relay, game processes, and real local TCP sockets outside the sandbox. This skill standardizes the process, player, and service-broadcast integration workflows.
---

# IPC Integration

Use this skill when the task is to run or verify real IPC integration behavior in this repository.

This skill is for:

- `game -> relay -> game` process delivery
- `game -> relay -> game` player receiver delivery
- service receiver broadcast

Do not use it for:

- pure unit tests
- protobuf generation checks
- in-process routing or receiver tests that already run in `build/tests/`

## Why Sandbox Escalation Is Needed

These scenarios need sandbox-external execution because they:

- start `etcd`
- bind local TCP ports
- run multiple long-lived processes
- verify real process-to-process traffic

The skill does not remove approval requirements. It only standardizes the workflow.

## Workflow

1. Build the required targets in `build/`.
2. Run the integration script outside the sandbox.
3. Check for the final `ok` line.

Script:

- `tools/ipc/run_integration.sh process`
- `tools/ipc/run_integration.sh player`
- `tools/ipc/run_integration.sh broadcast`

## Build Step

Run the matching build first:

```bash
cmake --build build --target relay game ipc_receiver_messaging_test ipc_discovery_routing_test
```

## Success Criteria

- `process`: target game reports `process_dispatch_count=1`
- `player`: target game reports `player_dispatch_count=1`
- `broadcast`: both game processes report `local_service_dispatch_count=1`

## Notes

- The script writes temporary configs and logs under `/tmp`.
- Set `KEEP_WORK_DIR=1` to preserve logs for debugging.
- Set `BUILD_DIR=/path/to/build` if not using the default repo `build/` tree.

## Repository Layout

- skill: `.agent/skills/ipc-integration/SKILL.md`
- script: `tools/ipc/run_integration.sh`
