#!/usr/bin/env bash
set -euo pipefail

scenario="${1:-}"
if [[ -z "${scenario}" ]]; then
    echo "usage: $0 <process|player|broadcast>" >&2
    exit 1
fi

case "${scenario}" in
    process|player|broadcast) ;;
    *)
        echo "unknown scenario: ${scenario}" >&2
        exit 1
        ;;
esac

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${BUILD_DIR:-${root_dir}/build}"
bin_dir="${build_dir}/bin"
work_dir="${WORK_DIR:-$(mktemp -d "/tmp/some-server-ipc-${scenario}-XXXXXX")}"
keep_work_dir="${KEEP_WORK_DIR:-0}"
etcd_bin="${ETCD_BIN:-etcd}"
etcdctl_bin="${ETCDCTL_BIN:-etcdctl}"
base_port="${BASE_PORT:-$((20000 + RANDOM % 20000))}"
etcd_client_port="${ETCD_CLIENT_PORT:-${base_port}}"
etcd_peer_port="${ETCD_PEER_PORT:-$((base_port + 1))}"
relay_port="${RELAY_PORT:-$((base_port + 2))}"
game1_port="${GAME1_PORT:-$((base_port + 3))}"
game2_port="${GAME2_PORT:-$((base_port + 4))}"
endpoint="127.0.0.1:${etcd_client_port}"
peer_endpoint="127.0.0.1:${etcd_peer_port}"
prefix="/some_server/ipc/integration/${scenario}"
lease_ttl_seconds=4

declare -A app_pids
declare -A app_fifos
declare -A app_fds
declare -A app_logs
etcd_pid=""

require_binary() {
    local path="$1"
    if [[ ! -x "${path}" ]]; then
        echo "missing executable: ${path}" >&2
        exit 1
    fi
}

cleanup() {
    set +e
    for name in relay game1 game2; do
        if [[ -n "${app_fds[${name}]:-}" ]]; then
            printf 'exit\n' >&"${app_fds[${name}]}" 2>/dev/null || true
        fi
    done
    sleep 1
    for name in relay game1 game2; do
        if [[ -n "${app_pids[${name}]:-}" ]]; then
            kill "${app_pids[${name}]}" 2>/dev/null || true
            wait "${app_pids[${name}]}" 2>/dev/null || true
        fi
    done
    if [[ -n "${etcd_pid}" ]]; then
        kill "${etcd_pid}" 2>/dev/null || true
        wait "${etcd_pid}" 2>/dev/null || true
    fi
    if [[ "${keep_work_dir}" != "1" ]]; then
        rm -rf "${work_dir}"
    else
        echo "kept work directory: ${work_dir}"
    fi
}
trap cleanup EXIT

write_config() {
    local file="$1"
    local app_name="$2"
    local instance_id="$3"
    local port="$4"
    local log_name="$5"
    cat >"${file}" <<EOF
{
  "log": {
    "file": "${work_dir}/${log_name}.log",
    "error_file": "${work_dir}/${log_name}.error.log",
    "console": true
  },
  "${app_name}": {
    "instance_id": ${instance_id},
    "listen": { "host": "127.0.0.1", "port": ${port} },
    "discovery": {
      "endpoints": ["${endpoint}"],
      "prefix": "${prefix}",
      "lease_ttl_seconds": ${lease_ttl_seconds}
    }
  }
}
EOF
}

wait_for_etcd() {
    local attempts=30
    local count=0
    until "${etcdctl_bin}" --endpoints="${endpoint}" endpoint health >/dev/null 2>&1; do
        ((count += 1))
        if [[ "${count}" -ge "${attempts}" ]]; then
            echo "etcd did not become healthy" >&2
            exit 1
        fi
        sleep 1
    done
}

start_etcd() {
    "${etcd_bin}" \
        --name "ipc-${scenario}" \
        --data-dir "${work_dir}/etcd" \
        --listen-client-urls "http://${endpoint}" \
        --advertise-client-urls "http://${endpoint}" \
        --listen-peer-urls "http://${peer_endpoint}" \
        --initial-advertise-peer-urls "http://${peer_endpoint}" \
        --initial-cluster "ipc-${scenario}=http://${peer_endpoint}" \
        >"${work_dir}/etcd.log" 2>&1 &
    etcd_pid="$!"
    wait_for_etcd
}

start_app() {
    local name="$1"
    local binary="$2"
    local config="$3"
    local fifo="${work_dir}/${name}.fifo"
    local log="${work_dir}/${name}.stdout.log"
    mkfifo "${fifo}"
    app_fifos["${name}"]="${fifo}"
    app_logs["${name}"]="${log}"
    exec {fd}<>"${fifo}"
    app_fds["${name}"]="${fd}"
    bash -lc "cd '${bin_dir}' && cat '${fifo}' | './${binary}' -c '${config}'" >"${log}" 2>&1 &
    app_pids["${name}"]="$!"
    sleep 1
}

send_cmd() {
    local name="$1"
    local command="$2"
    if [[ -z "${app_fds[${name}]:-}" ]]; then
        echo "app command channel is not initialized: ${name}" >&2
        exit 1
    fi
    printf '%s\n' "${command}" >&"${app_fds[${name}]}"
    sleep 1
}

assert_log_contains() {
    local name="$1"
    local pattern="$2"
    local attempts="${3:-20}"
    local count=0
    until grep -Fq "${pattern}" "${app_logs[${name}]}"; do
        ((count += 1))
        if [[ "${count}" -ge "${attempts}" ]]; then
            echo "expected pattern not found in ${name} log: ${pattern}" >&2
            echo "--- ${name} log ---" >&2
            cat "${app_logs[${name}]}" >&2
            exit 1
        fi
        sleep 1
    done
}

bootstrap_cluster() {
    send_cmd relay "ipc_refresh"
    send_cmd game1 "ipc_refresh"
    send_cmd game2 "ipc_refresh"
    send_cmd relay "ipc_connect 10 1"
    send_cmd relay "ipc_connect 10 2"
    sleep 2
    send_cmd relay "ipc_links"
    send_cmd game1 "ipc_links"
    send_cmd game2 "ipc_links"
    assert_log_contains relay "relay ipc links: count=2"
    assert_log_contains game1 "game ipc links: count=1"
    assert_log_contains game2 "game ipc links: count=1"
}

run_process_scenario() {
    send_cmd game1 "ipc_send_process 2 relay-process"
    send_cmd game2 "ipc_status"
    assert_log_contains game2 "process_dispatch_count=1"
    assert_log_contains game2 "last_process_payload_type=type.googleapis.com/google.protobuf.StringValue"
}

run_player_scenario() {
    send_cmd game2 "ipc_bind_player_local 1001"
    send_cmd game1 "ipc_bind_player_remote 1001 2"
    send_cmd game1 "ipc_send_player 1001 hello-player"
    send_cmd game2 "ipc_status"
    assert_log_contains game2 "player_dispatch_count=1"
    assert_log_contains game2 "last_player_id=1001"
    assert_log_contains game2 "last_player_payload_type=type.googleapis.com/google.protobuf.StringValue"
}

run_broadcast_scenario() {
    send_cmd game1 "ipc_broadcast_service hello-broadcast 1"
    send_cmd game1 "ipc_status"
    send_cmd game2 "ipc_status"
    assert_log_contains game1 "local_service_dispatch_count=1"
    assert_log_contains game2 "local_service_dispatch_count=1"
    assert_log_contains game1 "last_payload_type=type.googleapis.com/google.protobuf.StringValue"
    assert_log_contains game2 "last_payload_type=type.googleapis.com/google.protobuf.StringValue"
}

require_binary "${bin_dir}/relay"
require_binary "${bin_dir}/game"
command -v "${etcd_bin}" >/dev/null 2>&1 || { echo "missing etcd executable: ${etcd_bin}" >&2; exit 1; }
command -v "${etcdctl_bin}" >/dev/null 2>&1 || { echo "missing etcdctl executable: ${etcdctl_bin}" >&2; exit 1; }

mkdir -p "${work_dir}"
write_config "${work_dir}/relay.json" "relay" 1 "${relay_port}" "relay"
write_config "${work_dir}/game1.json" "game" 1 "${game1_port}" "game1"
write_config "${work_dir}/game2.json" "game" 2 "${game2_port}" "game2"

start_etcd
start_app relay "relay" "${work_dir}/relay.json"
start_app game1 "game" "${work_dir}/game1.json"
start_app game2 "game" "${work_dir}/game2.json"
bootstrap_cluster

case "${scenario}" in
    process)
        run_process_scenario
        ;;
    player)
        run_player_scenario
        ;;
    broadcast)
        run_broadcast_scenario
        ;;
esac

echo "ipc integration scenario '${scenario}': ok"
