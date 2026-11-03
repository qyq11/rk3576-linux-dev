#!/usr/bin/env bash
set -Eeuo pipefail
cd /home/rkshare/programs/sn100_shell/verification/status_login_09_20261103
[[ $(sha256sum upload_manifest_09.sha256 | cut -d' ' -f1) == 037867e5ba069802665d3c0b50670160512e91580a4dfa540a37f7afed346cdf ]]
sha256sum --quiet -c upload_manifest_09.sha256
[[ $(sha256sum /home/rkshare/programs/sn100_shell/verification/status_login_08_permission_20261103/runs/auto_permission_01/ini_before.sha256 | cut -d' ' -f1) == 6c961ce4eff4981afa1e7b0e59711fb90eee8b45395e83e2b4049bff6fd58c38 ]]
sha256sum -c /home/rkshare/programs/sn100_shell/verification/status_login_08_permission_20261103/runs/auto_permission_01/ini_before.sha256
[[ $(sha256sum /home/rkshare/programs/sn100_shell/verification/status_login_08_permission_20261103/runs/auto_permission_01/controller_ini_before.sha256 | cut -d' ' -f1) == d82b216b3aee25fb0298565b73a62d7144ede9a1aae1d6840bce6da8ace3b112 ]]
sha256sum -c /home/rkshare/programs/sn100_shell/verification/status_login_08_permission_20261103/runs/auto_permission_01/controller_ini_before.sha256

# 同一次派发只读采样原主控，用于和本轮上位机短窗并列；不发控制命令。
sample_dir=$PWD/controller_resources_revision05_01
[[ ! -e $sample_dir && ! -L $sample_dir ]]
mkdir -m 750 "$sample_dir"
controller=
for item in /proc/[0-9]*/exe;do
 if [[ $(readlink "$item" || true) == /userdata/shared/ACA_V1.0OffLineD-500 ]];then
  [[ -z $controller ]];controller=${item#/proc/};controller=${controller%/exe}
 fi
done
[[ -n $controller ]]
[[ $(sha256sum "/proc/$controller/exe" | cut -d' ' -f1) == ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff ]]
sample_boot=$(cat /proc/sys/kernel/random/boot_id)
stat_start=$(cat "/proc/$controller/stat");rest_start=${stat_start##*) };read -r -a start_fields <<<"$rest_start"
sample_start=${start_fields[19]}
sample_controller() {
 printf 'uptime_s\tutime_ticks\tstime_ticks\trss_kib\tpss_kib\tthreads\tfd_count\n'
 for index in $(seq 0 185);do
  [[ ! -e $sample_dir/stop ]] || return 0
  [[ $(cat /proc/sys/kernel/random/boot_id) == "$sample_boot" && $(readlink "/proc/$controller/exe") == /userdata/shared/ACA_V1.0OffLineD-500 ]] || return 1
  stat=$(cat "/proc/$controller/stat");rest=${stat##*) };read -r -a fields <<<"$rest"
  [[ ${fields[19]} == "$sample_start" ]] || return 1
  read -r uptime _ </proc/uptime
  rss=$(awk '$1=="VmRSS:"{print $2}' "/proc/$controller/status")
  pss=$(awk '$1=="Pss:"{print $2}' "/proc/$controller/smaps_rollup")
  threads=$(awk '$1=="Threads:"{print $2}' "/proc/$controller/status")
  fds=$(find "/proc/$controller/fd" -mindepth 1 -maxdepth 1 -type l | wc -l)
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$uptime" "${fields[11]}" "${fields[12]}" "$rss" "$pss" "$threads" "$fds"
  sleep 1
 done
 return 1
}
sample_controller >"$sample_dir/resources.tsv" & sampler=$!
set +e
bash run_case_on_board.sh auto b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993 5b108ad35472709fccde35c4cc63d4be64637dcfc7e087839122a548c548d238 3be27fac0eb30ed764c7f2ca27f070ef6558f4a34dbbdd5e6e77d226bda18b24 180 auto_revision05_01
actual_case_exit=$?
printf '1\n' >"$sample_dir/stop"
wait "$sampler";sample_exit=$?
set -e
printf '%s\n' "$sample_exit" >"$sample_dir/sample.exit"
printf 'CONTROLLER_RESOURCE_BEGIN\n';cat "$sample_dir/resources.tsv";printf 'CONTROLLER_RESOURCE_END\n'
printf 'CONTROLLER_RESOURCE_EXIT=%s\nCONTROLLER_RESOURCE_BOOT=%s\nCONTROLLER_RESOURCE_PID=%s\nCONTROLLER_RESOURCE_START_TICKS=%s\nCONTROLLER_RESOURCE_EXE=/userdata/shared/ACA_V1.0OffLineD-500\nCONTROLLER_RESOURCE_EXE_SHA256=ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff\nCONTROLLER_RESOURCE_CLK_TCK=%s\nACTUAL_CASE_EXIT=%s\n' "$sample_exit" "$sample_boot" "$controller" "$sample_start" "$(getconf CLK_TCK)" "$actual_case_exit"
exit "$actual_case_exit"
