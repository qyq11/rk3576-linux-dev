#!/usr/bin/env bash
set -euo pipefail
src="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$src/../../.." && pwd)"
build="$root/temp/sn100_host/reagents/wsl_release"
mkdir -p "$build"
cd "$build"
qmake "$src/reagents.pro" CONFIG+=release CONFIG-=debug
make -j2
if [[ "${1:-}" == build ]]; then exit 0; fi
exec "$build/sn100_reagents" "$@"
