#!/usr/bin/env bash
set -Eeuo pipefail
directory=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# The viewport method asserts native screenAt/widgetAt reachability; an
# offscreen platform has no physical screen hit target. Preserve that failed run.
timeout 180 bash "$directory/run_case.sh" offscreen integrated initializationFailureExperiment
timeout 180 bash "$directory/run_case.sh" offscreen status
for method in writeExperiment readExperiment viewportExperiment initializationFailureExperiment; do
    timeout 180 bash "$directory/run_case.sh" xcb integrated "$method"
done
timeout 180 bash "$directory/run_case.sh" xcb status
timeout 90 bash "$directory/run_case.sh" xcb integrated applicationExitExperiment
