#!/usr/bin/env bash
set -euo pipefail

target="${1:-}"
if [[ -z "${target}" ]]; then
    echo "usage: $0 <artifact-directory|latest-scenario-link>" >&2
    exit 1
fi

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${BUILD_DIR:-${root_dir}/build}"
artifact_root="${ARTIFACT_ROOT:-${build_dir}/test-artifacts/sim_client}"

resolve_target() {
    local input="$1"
    if [[ -d "${input}" || -L "${input}" ]]; then
        readlink -f "${input}"
        return
    fi
    if [[ -d "${artifact_root}/${input}" || -L "${artifact_root}/${input}" ]]; then
        readlink -f "${artifact_root}/${input}"
        return
    fi
    echo "artifact target not found: ${input}" >&2
    exit 1
}

artifact_dir="$(resolve_target "${target}")"
if [[ ! -d "${artifact_dir}" ]]; then
    echo "artifact directory does not exist: ${artifact_dir}" >&2
    exit 1
fi

show_file_tail() {
    local label="$1"
    local file="$2"
    local lines="${3:-20}"
    if [[ ! -f "${file}" ]]; then
        return
    fi
    echo "== ${label}: ${file}"
    tail -n "${lines}" "${file}"
    echo
}

show_matching_lines() {
    local label="$1"
    local file="$2"
    local pattern="$3"
    local limit="${4:-10}"
    if [[ ! -f "${file}" ]]; then
        return
    fi
    local matches
    matches="$(grep -Ein "${pattern}" "${file}" | tail -n "${limit}" || true)"
    if [[ -z "${matches}" ]]; then
        return
    fi
    echo "== ${label}: ${file}"
    printf '%s\n' "${matches}"
    echo
}

show_last_status_line() {
    local label="$1"
    local file="$2"
    if [[ ! -f "${file}" ]]; then
        return
    fi
    local line
    line="$(grep -F "sim_client status:" "${file}" | tail -n 1 || true)"
    if [[ -z "${line}" ]]; then
        return
    fi
    echo "== ${label}: ${file}"
    printf '%s\n' "${line}"
    echo
}

echo "artifact_dir=${artifact_dir}"
if [[ -f "${artifact_dir}/artifact_reason.txt" ]]; then
    echo "artifact_reason=$(cat "${artifact_dir}/artifact_reason.txt")"
fi
echo

echo "configs:"
find "${artifact_dir}" -maxdepth 1 -type f \( -name '*.json' -o -name 'artifact_reason.txt' \) | sort
echo

echo "signals:"
show_last_status_line "sim1 last status" "${artifact_dir}/sim1.stdout.log"
show_last_status_line "sim2 last status" "${artifact_dir}/sim2.stdout.log"
show_matching_lines "sim1 key lines" "${artifact_dir}/sim1.stdout.log" 'last_login_ok=|last_login_error_message=|last_kick_reason=|last_player_response_error_message=|player lease|maria'
show_matching_lines "sim2 key lines" "${artifact_dir}/sim2.stdout.log" 'last_login_ok=|last_login_error_message=|last_kick_reason=|last_player_response_error_message=|player lease|maria'
show_matching_lines "gate key lines" "${artifact_dir}/gate.log" 'kick|ownership moved|healthy_relay_link=|last_error=|error'
show_matching_lines "game1 key lines" "${artifact_dir}/game1.log" 'player lease|maria|flush|detached|online|last_error=|error'
show_matching_lines "game2 key lines" "${artifact_dir}/game2.log" 'player lease|maria|flush|detached|online|last_error=|error'
show_matching_lines "relay key lines" "${artifact_dir}/relay.log" 'links: count=|last_error=|error'

for name in etcd relay game1 game2 gate sim1 sim2; do
    show_file_tail "${name} stdout" "${artifact_dir}/${name}.stdout.log" 25
    show_file_tail "${name} app log" "${artifact_dir}/${name}.log" 15
    show_file_tail "${name} error log" "${artifact_dir}/${name}.error.log" 15
done
