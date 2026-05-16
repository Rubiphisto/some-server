#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
PROTO_ROOT="${REPO_ROOT}/proto/game"
OUT_DIR="${REPO_ROOT}/src/protocol/game/pb"

PROTOC_BIN="${PROTOC_BIN:-protoc}"

mkdir -p "${OUT_DIR}"

"${PROTOC_BIN}" \
  --proto_path="${PROTO_ROOT}" \
  --cpp_out="${OUT_DIR}" \
  "${PROTO_ROOT}/common.proto" \
  "${PROTO_ROOT}/login.proto" \
  "${PROTO_ROOT}/message_ids.proto" \
  "${PROTO_ROOT}/player.proto" \
  "${PROTO_ROOT}/player_data.proto"
