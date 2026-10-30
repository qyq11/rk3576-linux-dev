#!/usr/bin/env bash
set -Eeuo pipefail
umask 077
base=/home/rkshare/programs/sn100_shell
work=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
[[ $work =~ ^/home/rkshare/programs/sn100_shell/verification/button_audit_[0-9][0-9]$ ]]
[[ $(id -un) == lckfb && $(uname -m) == aarch64 && $EUID != 0 ]]
exec > "$work/run.log" 2>&1
exec 9>"$base/lifecycle.lock"
flock -n 9 || { echo 'AUDIT_BLOCKED=lifecycle_in_use'; exit 4; }
release=$(cat "$work/release_path")
[[ $release == "$base/releases/"* && ${release#"$base/releases/"} =~ ^[a-f0-9]{64}$ ]]
(cd "$release" && sha256sum --quiet -c SHA256SUMS)
source "$release/rk_board.sh"
runtime_environment "$release" "$work/cache"
export XDG_CONFIG_HOME=$work/xdg/config XDG_DATA_HOME=$work/xdg/data
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
cat > "$work/kms.json" <<'KMS'
{"device":"/dev/dri/card0","hwcursor":false,"outputs":[{"name":"HDMI1","mode":"preferred","primary":true},{"name":"DP1","mode":"off"},{"name":"Writeback1","mode":"off"}]}
KMS
mkdir -m 700 "$work/xdg_runtime"
export XDG_RUNTIME_DIR=$work/xdg_runtime QT_QPA_EGLFS_KMS_CONFIG=$work/kms.json
export SN100_BUTTON_AUDIT_OUTPUT=$work/events.jsonl
export LD_PRELOAD=$work/libbutton_audit.so
ldd "$work/libbutton_audit.so" > "$work/ldd.txt"
! grep -q 'not found' "$work/ldd.txt"
printf 'RELEASE=%s\nARCH=%s\nQT_PLATFORM=%s\n' "$release" "$(uname -m)" "$QT_QPA_PLATFORM"
sha256sum "$release/sn100_shell" "$work/libbutton_audit.so" > "$work/binary_sha256.txt"
head -c 24 /dev/urandom | base64 > "$work/secret"
trap 'rm -f -- "$work/secret"' EXIT
unset LD_PRELOAD
profile=$(cat "$work/profile")
case $profile in
  generic) profile_args=(--seed-catalog-demo) ;;
  original) profile_args=(--original-db-root "$work/original_fixture") ;;
  *) exit 2 ;;
esac
printf 'AUDIT_PROFILE=%s\n' "$profile"
LD_PRELOAD=$work/libbutton_audit.so "$release/sn100_shell" --offline --accounts --history --catalog --samples --instrument \
  --password-login --printer-settings --lis-settings --quality-control --calibration --alarm-history --lis-raw-inbox \
  --data-dir "$work/data" --viewport 1600x900 --seed-anonymous --test-secret-file "$work/secret" \
  "${profile_args[@]}" --test-session offlineAdmin > "$work/app.log" 2>&1 &
pid=$!
echo "$pid" > "$work/app.pid"
printf 'uptime_s\tutime_ticks\tstime_ticks\trss_kib\tpss_kib\tthreads\tfd_count\n' > "$work/resources.tsv"
while kill -0 "$pid" 2>/dev/null; do
  if [[ -r /proc/$pid/stat ]]; then
    stat=$(cat /proc/$pid/stat) || break
    rest=${stat##*) }; set -- $rest
    rss=$(awk '$1=="VmRSS:"{print $2}' /proc/$pid/status) || rss=unavailable
    pss=$(awk '$1=="Pss:"{print $2}' /proc/$pid/smaps_rollup) || pss=unavailable
    threads=$(awk '$1=="Threads:"{print $2}' /proc/$pid/status) || threads=unavailable
    fds=$(find /proc/$pid/fd -maxdepth 1 -type l | wc -l) || fds=unavailable
    read -r uptime _ < /proc/uptime
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$uptime" "${12}" "${13}" "$rss" "$pss" "$threads" "$fds" >> "$work/resources.tsv"
  fi
  sleep 1
done
set +e
wait "$pid"
code=$?
set -e
echo "$code" > "$work/exit_code"
printf 'AUDIT_APP_EXIT=%s\n' "$code"
exit "$code"
