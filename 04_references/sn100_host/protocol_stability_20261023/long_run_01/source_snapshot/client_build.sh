#!/usr/bin/env bash
# Same offline ARM64 environment, separate executable and build directory.
set -Eeuo pipefail
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export SN100_PROTOCOL_TARGET=client
exec bash "$module_dir/transport_build.sh" "$@"
