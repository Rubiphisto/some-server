#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
runner="${root_dir}/tools/sim_client/run_regression.sh"
summary="${root_dir}/tools/sim_client/summarize_artifact.sh"

if [[ ! -x "${runner}" ]]; then
    echo "missing executable regression runner: ${runner}" >&2
    exit 1
fi

declare -a scenarios
if [[ "$#" -gt 0 ]]; then
    scenarios=("$@")
else
    scenarios=(
        reconnect_after_disconnect
        same_account_kick
        migration_recover_after_kick
        default_player_recover_after_maria_restore
        player_directory_persist_across_restart
    )
fi

for scenario in "${scenarios[@]}"; do
    echo "==> sim_client regression: ${scenario}"
    if ! "${runner}" "${scenario}"; then
        if [[ -x "${summary}" ]]; then
            echo
            echo "==> sim_client regression summary: ${scenario}"
            "${summary}" "latest-${scenario}" || true
        fi
        exit 1
    fi
done

echo "sim_client regressions: all ok"
