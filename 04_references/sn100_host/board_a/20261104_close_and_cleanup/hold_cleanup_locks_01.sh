#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
exec 9<"$root/temp/sn100_host/workflow_next_20261103/integration_11_01/arm64_build/build.lock"
exec 10<"$root/temp/sn100_host/workflow_next_20261103/integration_12_01/arm64_build/build.lock"
exec 11<"$root/temp/sn100_host/workflow_next_20261103/integration_14_01/arm64_build/build.lock"
exec 12<"$root/temp/sn100_host/workflow_next_20261103/integration_15_01/arm64_build/build.lock"
flock -n 9;flock -n 10;flock -n 11;flock -n 12
printf 'FOUR_BUILD_LOCKS_HELD_FOR_APPROVED_OBJECT_CLEANUP=1\n'
IFS= read -r release_token
[[ $release_token == RELEASE ]]
printf 'BUILD_LOCKS_RELEASED=1\n'
