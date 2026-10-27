#!/usr/bin/env bash
set -Eeuo pipefail
directory=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
for platform in offscreen xcb; do
    for method in writeExperiment readExperiment viewportExperiment initializationFailureExperiment; do
        timeout 180 bash "$directory/run_case.sh" "$platform" integrated "$method"
    done
    timeout 180 bash "$directory/run_case.sh" "$platform" status
done
timeout 90 bash "$directory/run_case.sh" xcb integrated applicationExitExperiment
