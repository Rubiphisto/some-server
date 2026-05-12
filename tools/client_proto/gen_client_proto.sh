#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
PROTO_ROOT="${REPO_ROOT}/proto/client"
OUT_DIR="${REPO_ROOT}/src/protocol/client/pb"

PROTOC_BIN="${PROTOC_BIN:-protoc}"

mkdir -p "${OUT_DIR}"

"${PROTOC_BIN}" \
  --proto_path="${PROTO_ROOT}" \
  --cpp_out="${OUT_DIR}" \
  "${PROTO_ROOT}/common/v1/types.proto" \
  "${PROTO_ROOT}/login/v1/login.proto" \
  "${PROTO_ROOT}/game/v1/player.proto"
