#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${BUILD_DIR:-${root_dir}/build}"
build_jobs="${BUILD_JOBS:-4}"
ctest_regex="${CTEST_REGEX:-sim_client_regression_}"

cmake -S "${root_dir}" -B "${build_dir}" -DENABLE_HOST_INTEGRATION_TESTS=ON
cmake --build "${build_dir}" --target relay gate game sim_client -j "${build_jobs}"
ctest --test-dir "${build_dir}" -R "${ctest_regex}" --output-on-failure "$@"
