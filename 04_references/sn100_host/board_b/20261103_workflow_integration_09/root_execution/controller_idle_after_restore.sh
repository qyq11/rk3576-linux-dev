#!/usr/bin/env bash
# 只读采样当前唯一原主控，不发命令、不启停程序、不制造负载。
set -Eeuo pipefail
[[ $(cat /proc/sys/kernel/random/boot_id) == 0eb94389-f120-42ad-9042-9c21d96a1481 ]]
controller=
apps=0
for item in /proc/[0-9]*/exe; do
 target=$(readlink "$item" 2>/dev/null || true)
 case "$target" in
  /userdata/shared/ACA_V1.0OffLineD-500) [[ -z $controller ]];[[ $(cat /proc/sys/kernel/random/boot_id) == 0eb94389-f120-42ad-9042-9c21d96a1481 ]]
controller=${item#/proc/};[[ $(cat /proc/sys/kernel/random/boot_id) == 0eb94389-f120-42ad-9042-9c21d96a1481 ]]
controller=${controller%/exe} ;;
  */sn100_shell|*/sn100_report_renderer|*/status_login*review*) apps=$((apps+1));;
 esac
done
[[ -n $controller && $apps == 0 ]]
[[ $(sha256sum "/proc/$controller/exe" | cut -d' ' -f1) == ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff ]]
ss -H -ltnp 'sport = :5000' | grep -q "pid=$controller,"
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nCONTROLLER_PID=%s\nAPPLICATION_COUNT=0\nCLK_TCK=%s\n' "$controller" "$(getconf CLK_TCK)"
printf 'BOOT_ID=';cat /proc/sys/kernel/random/boot_id
date -Is
printf 'uptime_s\tutime_ticks\tstime_ticks\trss_kib\tpss_kib\tthreads\tfd_count\n'
for index in $(seq 0 20); do
 [[ $(readlink "/proc/$controller/exe") == /userdata/shared/ACA_V1.0OffLineD-500 ]]
 stat=$(cat "/proc/$controller/stat");rest=${stat##*) };read -r -a fields <<<"$rest"
 read -r uptime _ </proc/uptime
 rss=$(awk '$1=="VmRSS:"{print $2}' "/proc/$controller/status")
 pss=$(awk '$1=="Pss:"{print $2}' "/proc/$controller/smaps_rollup")
 threads=$(awk '$1=="Threads:"{print $2}' "/proc/$controller/status")
 fds=$(find "/proc/$controller/fd" -mindepth 1 -maxdepth 1 -type l | wc -l)
 printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$uptime" "${fields[11]}" "${fields[12]}" "$rss" "$pss" "$threads" "$fds"
 if [[ $index != 20 ]];then sleep 1;fi
done
printf 'READ_ONLY=1\nHARDWARE_COMMANDS_SENT=0\nLOAD_CREATED=0\n'
