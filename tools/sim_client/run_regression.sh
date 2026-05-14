#!/usr/bin/env bash
set -euo pipefail

scenario="${1:-}"
if [[ -z "${scenario}" ]]; then
    echo "usage: $0 <reconnect_after_disconnect|same_account_kick|migration_recover_after_kick|default_player_recover_after_maria_restore|player_directory_persist_across_restart>" >&2
    exit 1
fi

case "${scenario}" in
    reconnect_after_disconnect|same_account_kick|migration_recover_after_kick|default_player_recover_after_maria_restore|player_directory_persist_across_restart) ;;
    *)
        echo "unknown scenario: ${scenario}" >&2
        exit 1
        ;;
esac

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${BUILD_DIR:-${root_dir}/build}"
bin_dir="${build_dir}/bin"
keep_work_dir="${KEEP_WORK_DIR:-0}"
keep_artifacts="${KEEP_ARTIFACTS:-0}"
artifact_root="${ARTIFACT_ROOT:-${build_dir}/test-artifacts/sim_client}"
mkdir -p "${artifact_root}"
if [[ -n "${WORK_DIR:-}" ]]; then
    work_dir="${WORK_DIR}"
else
    work_dir="$(mktemp -d "${artifact_root}/${scenario}-run-XXXXXX")"
fi
etcd_bin="${ETCD_BIN:-etcd}"
etcdctl_bin="${ETCDCTL_BIN:-etcdctl}"
redis_cli_bin="${REDIS_CLI_BIN:-redis-cli}"
base_port="${BASE_PORT:-$((24000 + RANDOM % 10000))}"
etcd_client_port="${ETCD_CLIENT_PORT:-${base_port}}"
etcd_peer_port="${ETCD_PEER_PORT:-$((base_port + 1))}"
relay_port="${RELAY_PORT:-$((base_port + 2))}"
game1_port="${GAME1_PORT:-$((base_port + 3))}"
game2_port="${GAME2_PORT:-$((base_port + 4))}"
gate_ipc_port="${GATE_IPC_PORT:-$((base_port + 5))}"
gate_client_port="${GATE_CLIENT_PORT:-$((base_port + 6))}"
endpoint="127.0.0.1:${etcd_client_port}"
peer_endpoint="127.0.0.1:${etcd_peer_port}"
prefix="/some_server/player_system/regression/${scenario}"
lease_ttl_seconds=4

declare -A app_pids
declare -A app_fifos
declare -A app_fds
declare -A app_logs
etcd_pid=""
artifacts_kept_reason=""

note_artifact_directory() {
    local reason="$1"
    local latest_link="${artifact_root}/latest-${scenario}"
    local summary_script="${root_dir}/tools/sim_client/summarize_artifact.sh"
    printf '%s\n' "${reason}" >"${work_dir}/artifact_reason.txt"
    rm -f "${latest_link}"
    ln -s "${work_dir}" "${latest_link}"
    echo "sim_client regression artifacts kept (${reason}): ${work_dir}"
    if [[ -x "${summary_script}" ]]; then
        echo "artifact summary: ${summary_script} ${work_dir}"
    fi
}

require_binary() {
    local path="$1"
    if [[ ! -x "${path}" ]]; then
        echo "missing executable: ${path}" >&2
        exit 1
    fi
}

cleanup() {
    local exit_code=$?
    set +e
    for name in relay game1 game2 gate sim1 sim2; do
        if [[ -n "${app_fds[${name}]:-}" ]]; then
            printf 'exit\n' >&"${app_fds[${name}]}" 2>/dev/null || true
        fi
    done
    sleep 1
    for name in relay game1 game2 gate sim1 sim2; do
        if [[ -n "${app_pids[${name}]:-}" ]]; then
            kill "${app_pids[${name}]}" 2>/dev/null || true
            wait "${app_pids[${name}]}" 2>/dev/null || true
        fi
    done
    if [[ -n "${etcd_pid}" ]]; then
        kill "${etcd_pid}" 2>/dev/null || true
        wait "${etcd_pid}" 2>/dev/null || true
    fi
    if [[ "${exit_code}" -ne 0 ]]; then
        artifacts_kept_reason="failed"
    elif [[ "${keep_work_dir}" == "1" ]]; then
        artifacts_kept_reason="keep_work_dir"
    elif [[ "${keep_artifacts}" == "1" ]]; then
        artifacts_kept_reason="keep_artifacts"
    fi
    if [[ -n "${artifacts_kept_reason}" ]]; then
        note_artifact_directory "${artifacts_kept_reason}"
    else
        rm -rf "${work_dir}"
    fi
    exit "${exit_code}"
}
trap cleanup EXIT

write_relay_config() {
    cat >"$1" <<EOF
{
  "log": {
    "file": "${work_dir}/relay.log",
    "error_file": "${work_dir}/relay.error.log",
    "console": true
  },
  "relay": {
    "instance_id": 1,
    "listen": { "host": "127.0.0.1", "port": ${relay_port} },
    "discovery": {
      "endpoints": ["${endpoint}"],
      "prefix": "${prefix}",
      "lease_ttl_seconds": ${lease_ttl_seconds}
    }
  }
}
EOF
}

write_game_config() {
    local file="$1"
    local instance_id="$2"
    local port="$3"
    local log_name="$4"
    cat >"${file}" <<EOF
{
  "log": {
    "file": "${work_dir}/${log_name}.log",
    "error_file": "${work_dir}/${log_name}.error.log",
    "console": true
  },
  "redis": {
    "default": {
      "endpoints": ["127.0.0.1:6379"],
      "password": "",
      "database": 0,
      "key_prefix": "some_server:"
    }
  },
  "maria": {
    "default": {
      "host": "127.0.0.1",
      "port": 3306,
      "database": "some_game",
      "username": "game",
      "password": "game",
      "pool_size": 16
    }
  },
  "game": {
    "instance_id": ${instance_id},
    "listen": { "host": "127.0.0.1", "port": ${port} },
    "discovery": {
      "endpoints": ["${endpoint}"],
      "prefix": "${prefix}",
      "lease_ttl_seconds": ${lease_ttl_seconds}
    },
    "storage": {
      "defaults": {
        "alive_time_seconds": 86400,
        "landing_time_seconds": 1800,
        "landing_min_time_seconds": 60
      },
      "datasets": {
        "directory": {
          "redis": "default",
          "maria": "default",
          "redis_prefix": "player_directory:",
          "table_prefix": "player_directory_"
        },
        "player": {
          "redis": "default",
          "maria": "default",
          "redis_prefix": "player:",
          "table_prefix": "player_"
        }
      }
    }
  }
}
EOF
}

write_gate_config() {
    cat >"$1" <<EOF
{
  "log": {
    "file": "${work_dir}/gate.log",
    "error_file": "${work_dir}/gate.error.log",
    "console": true
  },
  "redis": {
    "default": {
      "endpoints": ["127.0.0.1:6379"],
      "password": "",
      "database": 0,
      "key_prefix": "some_server:"
    }
  },
  "gate": {
    "instance_id": 1,
    "listen": { "host": "127.0.0.1", "port": ${gate_ipc_port} },
    "client_listen": { "host": "127.0.0.1", "port": ${gate_client_port} },
    "discovery": {
      "endpoints": ["${endpoint}"],
      "prefix": "${prefix}",
      "lease_ttl_seconds": ${lease_ttl_seconds}
    }
  }
}
EOF
}

write_sim_config() {
    local file="$1"
    local account_id="$2"
    local log_name="$3"
    cat >"${file}" <<EOF
{
  "log": {
    "file": "${work_dir}/${log_name}.log",
    "error_file": "${work_dir}/${log_name}.error.log",
    "console": true
  },
  "sim_client": {
    "gate": {
      "host": "127.0.0.1",
      "port": ${gate_client_port}
    },
    "default_platform": "dev",
    "default_account_id": "${account_id}",
    "default_client_version": 1,
    "default_channel": "sim"
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
        --name "sim-${scenario}" \
        --data-dir "${work_dir}/etcd" \
        --listen-client-urls "http://${endpoint}" \
        --advertise-client-urls "http://${endpoint}" \
        --listen-peer-urls "http://${peer_endpoint}" \
        --initial-advertise-peer-urls "http://${peer_endpoint}" \
        --initial-cluster "sim-${scenario}=http://${peer_endpoint}" \
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

close_app_fd() {
    local name="$1"
    if [[ -n "${app_fds[${name}]:-}" ]]; then
        local fd="${app_fds[${name}]}"
        eval "exec ${fd}>&-"
        unset 'app_fds[$name]'
    fi
}

stop_app() {
    local name="$1"
    if [[ -n "${app_fds[${name}]:-}" ]]; then
        printf 'exit\n' >&"${app_fds[${name}]}" 2>/dev/null || true
    fi
    sleep 1
    if [[ -n "${app_pids[${name}]:-}" ]]; then
        kill "${app_pids[${name}]}" 2>/dev/null || true
        wait "${app_pids[${name}]}" 2>/dev/null || true
        unset 'app_pids[$name]'
    fi
    close_app_fd "${name}"
    if [[ -n "${app_fifos[${name}]:-}" ]]; then
        rm -f "${app_fifos[${name}]}"
        unset 'app_fifos[$name]'
    fi
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
    local attempts="${3:-25}"
    local count=0
    until grep -aFq "${pattern}" "${app_logs[${name}]}"; do
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

last_log_line_containing() {
    local name="$1"
    local pattern="$2"
    grep -aF "${pattern}" "${app_logs[${name}]}" | tail -n 1
}

assert_last_log_line_contains() {
    local name="$1"
    local anchor="$2"
    local pattern="$3"
    local attempts="${4:-25}"
    local count=0
    while true; do
        local line
        line="$(last_log_line_containing "${name}" "${anchor}" || true)"
        if [[ -n "${line}" && "${line}" == *"${pattern}"* ]]; then
            return
        fi
        ((count += 1))
        if [[ "${count}" -ge "${attempts}" ]]; then
            echo "expected last ${name} log line containing '${anchor}' to also contain '${pattern}'" >&2
            echo "--- ${name} log ---" >&2
            cat "${app_logs[${name}]}" >&2
            exit 1
        fi
        sleep 1
    done
}

extract_last_login_player_id() {
    local name="$1"
    local line
    line="$(last_log_line_containing "${name}" "sim_client status:" || true)"
    if [[ -z "${line}" ]]; then
        echo "failed to find sim_client status line in ${name} log" >&2
        exit 1
    fi
    local player_id
    player_id="$(printf '%s\n' "${line}" | sed -n 's/.*last_login_player_id=\([0-9][0-9]*\).*/\1/p')"
    if [[ -z "${player_id}" ]]; then
        echo "failed to extract last_login_player_id from ${name} status line" >&2
        echo "${line}" >&2
        exit 1
    fi
    printf '%s\n' "${player_id}"
}

bootstrap_cluster() {
    send_cmd relay "ipc_refresh"
    send_cmd game1 "ipc_refresh"
    send_cmd game2 "ipc_refresh"
    sleep 2
    send_cmd relay "ipc_links"
    send_cmd game1 "ipc_links"
    send_cmd game2 "ipc_links"
    send_cmd gate "ipc_status"
    assert_log_contains relay "relay ipc links: count=3"
    assert_log_contains game1 "game ipc links: count=1"
    assert_log_contains game2 "game ipc links: count=1"
    assert_log_contains gate "healthy_relay_link=true"
}

run_reconnect_after_disconnect() {
    send_cmd sim1 "scenario_run reconnect_after_disconnect sim-reconnect-1"
    send_cmd sim1 "status"
    assert_log_contains sim1 "last_scenario=reconnect_after_disconnect"
    assert_log_contains sim1 "connected=true"
    assert_log_contains sim1 "last_login_ok=true"
}

run_same_account_kick() {
    send_cmd sim1 "scenario_run login_smoke sim-kick-1"
    send_cmd sim2 "scenario_run login_smoke sim-kick-1"
    send_cmd sim1 "status"
    send_cmd sim2 "status"
    assert_log_contains sim1 "connected=false"
    assert_log_contains sim1 "last_kick_reason=same account logged in on a new connection"
    assert_log_contains sim2 "connected=true"
    assert_log_contains sim2 "last_login_ok=true"
}

run_migration_recover_after_kick() {
    local player_id=""
    send_cmd sim1 "scenario_run login_smoke sim-migrate-1"
    send_cmd sim1 "status"
    assert_log_contains sim1 "last_login_ok=true"
    player_id="$(extract_last_login_player_id sim1)"
    "${redis_cli_bin}" -h 127.0.0.1 -p 6379 DEL "some_server:game:player:lease:${player_id}" >/dev/null 2>&1 || true
    send_cmd game2 "player_activate ${player_id} 20 2 88"
    send_cmd sim1 "status"
    assert_log_contains sim1 "connected=false"
    assert_log_contains sim1 "last_kick_reason=player session ownership moved"
    send_cmd sim1 "scenario_run recover_after_kick sim-migrate-1"
    send_cmd sim1 "status"
    assert_log_contains sim1 "last_scenario=recover_after_kick"
    assert_log_contains sim1 "connected=true"
    assert_log_contains sim1 "last_login_ok=true"
}

run_default_player_recover_after_maria_restore() {
    local account_id="sim-maria-recover-${base_port}"
    local player_id=$((200000000 + base_port))
    "${redis_cli_bin}" -h 127.0.0.1 -p 6379 SET some_server:player_directory:directory:next_player_id "$((player_id - 1))" >/dev/null
    send_cmd game1 "storage_disable_maria default"
    send_cmd game1 "storage_probe"
    assert_log_contains game1 "storage maria probe: name=default"
    assert_log_contains game1 "disabled by runtime command"

    send_cmd sim1 "scenario_run login_smoke ${account_id}"
    send_cmd sim1 "status"
    assert_log_contains sim1 "last_login_ok=true"
    assert_log_contains sim1 "configured_account_id=${account_id}"
    if [[ "$(extract_last_login_player_id sim1)" != "${player_id}" ]]; then
        echo "unexpected player_id for ${account_id}" >&2
        echo "--- sim1 log ---" >&2
        cat "${app_logs[sim1]}" >&2
        exit 1
    fi

    send_cmd game1 "player_status ${player_id}"
    assert_last_log_line_contains game1 "player status: player_id=${player_id}" "pending_initial_persist=true"
    assert_last_log_line_contains game1 "player status: player_id=${player_id}" "created_without_maria=true"

    send_cmd game1 "storage_enable_maria default"
    send_cmd game1 "storage_probe"
    assert_log_contains game1 "storage enable maria: name=default"
    assert_last_log_line_contains game1 "storage maria probe: name=default" "reachable=true"

    send_cmd game1 "player_persistence_recover_flush"
    send_cmd game1 "player_persistence_player_status ${player_id}"
    send_cmd game1 "player_status ${player_id}"
    assert_last_log_line_contains game1 "player persistence player status: player_id=${player_id}" "pending_initial_persist=false"
    assert_last_log_line_contains game1 "player persistence player status: player_id=${player_id}" "created_without_maria=false"
    assert_last_log_line_contains game1 "player persistence player status: player_id=${player_id}" "flush_count=1"
    assert_last_log_line_contains game1 "player status: player_id=${player_id}" "pending_initial_persist=false"
    assert_last_log_line_contains game1 "player status: player_id=${player_id}" "created_without_maria=false"
}

extract_last_directory_player_id() {
    local name="$1"
    local anchor="$2"
    local line
    line="$(last_log_line_containing "${name}" "${anchor}" || true)"
    if [[ -z "${line}" ]]; then
        echo "failed to find directory log line in ${name} log: ${anchor}" >&2
        exit 1
    fi
    local player_id
    player_id="$(printf '%s\n' "${line}" | sed -n 's/.*player_id=\([0-9][0-9]*\).*/\1/p')"
    if [[ -z "${player_id}" ]]; then
        echo "failed to extract player_id from directory log line" >&2
        echo "${line}" >&2
        exit 1
    fi
    printf '%s\n' "${player_id}"
}

extract_last_directory_counter() {
    local name="$1"
    local line
    line="$(last_log_line_containing "${name}" "player directory counter:" || true)"
    if [[ -z "${line}" ]]; then
        echo "failed to find directory counter line in ${name} log" >&2
        exit 1
    fi
    local counter
    counter="$(printf '%s\n' "${line}" | sed -n 's/.*next_player_id=\([0-9][0-9]*\).*/\1/p')"
    if [[ -z "${counter}" ]]; then
        echo "failed to extract next_player_id from directory counter line" >&2
        echo "${line}" >&2
        exit 1
    fi
    printf '%s\n' "${counter}"
}

run_player_directory_persist_across_restart() {
    local account_id="sim-directory-persist-${base_port}"
    local before_player_id=""
    local after_player_id=""
    local before_counter=""
    local after_counter=""
    local restart_port=$((game1_port + 100))

    send_cmd game1 "player_directory_lookup dev ${account_id} 1"
    assert_last_log_line_contains game1 "player directory lookup: platform=dev account_id=${account_id} area_id=1" "present=false"

    send_cmd game1 "player_directory_resolve dev ${account_id} 1"
    before_player_id="$(extract_last_directory_player_id game1 "player directory resolve: platform=dev account_id=${account_id} area_id=1")"

    send_cmd game1 "player_directory_counter"
    before_counter="$(extract_last_directory_counter game1)"
    if [[ "${before_counter}" != "${before_player_id}" ]]; then
        echo "unexpected directory counter after create: counter=${before_counter} player_id=${before_player_id}" >&2
        exit 1
    fi

    stop_app game1
    write_game_config "${work_dir}/game1-restart.json" 1 "${restart_port}" "game1"
    start_app game1 "game" "${work_dir}/game1-restart.json"

    send_cmd game1 "player_directory_lookup dev ${account_id} 1"
    assert_last_log_line_contains game1 "player directory lookup: platform=dev account_id=${account_id} area_id=1" "present=true"
    after_player_id="$(extract_last_directory_player_id game1 "player directory lookup: platform=dev account_id=${account_id} area_id=1")"

    send_cmd game1 "player_directory_counter"
    after_counter="$(extract_last_directory_counter game1)"

    if [[ "${after_player_id}" != "${before_player_id}" ]]; then
        echo "directory mapping changed across restart: before=${before_player_id} after=${after_player_id}" >&2
        exit 1
    fi
    if [[ "${after_counter}" != "${before_counter}" ]]; then
        echo "directory counter changed across restart lookup: before=${before_counter} after=${after_counter}" >&2
        exit 1
    fi
}

require_binary "${bin_dir}/relay"
require_binary "${bin_dir}/game"
require_binary "${bin_dir}/gate"
require_binary "${bin_dir}/sim_client"
command -v "${etcd_bin}" >/dev/null 2>&1 || { echo "missing etcd executable: ${etcd_bin}" >&2; exit 1; }
command -v "${etcdctl_bin}" >/dev/null 2>&1 || { echo "missing etcdctl executable: ${etcdctl_bin}" >&2; exit 1; }
command -v "${redis_cli_bin}" >/dev/null 2>&1 || { echo "missing redis-cli executable: ${redis_cli_bin}" >&2; exit 1; }

mkdir -p "${work_dir}"
write_relay_config "${work_dir}/relay.json"
write_game_config "${work_dir}/game1.json" 1 "${game1_port}" "game1"
write_game_config "${work_dir}/game2.json" 2 "${game2_port}" "game2"
write_gate_config "${work_dir}/gate.json"
write_sim_config "${work_dir}/sim1.json" "sim-account-1" "sim1"
write_sim_config "${work_dir}/sim2.json" "sim-account-2" "sim2"

start_etcd
start_app relay "relay" "${work_dir}/relay.json"
start_app game1 "game" "${work_dir}/game1.json"
start_app game2 "game" "${work_dir}/game2.json"
start_app gate "gate" "${work_dir}/gate.json"
start_app sim1 "sim_client" "${work_dir}/sim1.json"
start_app sim2 "sim_client" "${work_dir}/sim2.json"
bootstrap_cluster

case "${scenario}" in
    reconnect_after_disconnect)
        run_reconnect_after_disconnect
        ;;
    same_account_kick)
        run_same_account_kick
        ;;
    migration_recover_after_kick)
        run_migration_recover_after_kick
        ;;
    default_player_recover_after_maria_restore)
        run_default_player_recover_after_maria_restore
        ;;
    player_directory_persist_across_restart)
        run_player_directory_persist_across_restart
        ;;
esac

echo "sim_client regression scenario '${scenario}': ok"
