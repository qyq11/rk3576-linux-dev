#!/usr/bin/env bash
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
[[ $(readlink /proc/4426/exe) == /userdata/shared/ACA_V1.0OffLineD-500 ]]
sha256sum /proc/4426/exe
cat /proc/4426/stat
ss -H -ltnp 'sport = :5000'
[[ ! -e /home/rkshare/programs/aca_flow_test/board_a_20261104_01/exit_code ]]
printf 'CONTROLLER_STARTUP_WARNINGS_ONLY\n'
grep -E 'failed|error|Error|GPIO|offline|OffLine|SN500|SN450|Version' /home/rkshare/programs/aca_flow_test/board_a_20261104_01/program.log | head -16 || true
printf 'CONTROLLER_SEPARATE_SESSION_ALIVE=1\n'
