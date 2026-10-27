#!/usr/bin/env bash
set -Eeuo pipefail
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export SN100_PROTOCOL_TARGET=soak
exec bash "$module_dir/transport_build.sh" "$@"
