set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f

base=/home/rkshare/programs/sn100_shell
new="$base/releases/e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270"
exec 9>"$base/lifecycle.lock"
flock -n 9
test -L "$base/current"
test "$(readlink "$base/current")" = 'releases/7c4abe3062ead1f5b6fc6fd495b708be5f11436504bba5e900b6ccb2c4f5d5f9'
test "$(sha256sum "$new/SHA256SUMS" | cut -d' ' -f1)" = 'e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270'
(cd "$new" && sha256sum --quiet -c SHA256SUMS)
test "$(sha256sum "$new/sn100_shell" | cut -d' ' -f1)" = '609aa4b9a60109df4df241181e4622626ab9b77fba2b272ca0643a5bcf5506dc'
controller_count=0
controller_pid=
for exe in /proc/[0-9]*/exe; do
    path=$(readlink "$exe" || true)
    case "$path" in
        */sn100_shell|*/status_login*review*) printf 'APP_ALREADY_RUNNING=%s\n' "$exe"; exit 3 ;;
        /userdata/shared/ACA_V1.0OffLineD-500) controller_count=$((controller_count+1)); controller_pid=${exe#/proc/}; controller_pid=${controller_pid%/exe} ;;
    esac
done
test "$controller_count" = 1
test "$(sha256sum /proc/$controller_pid/exe | cut -d' ' -f1)" = ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff
ss -H -ltnp 'sport = :5000' | grep -q "pid=$controller_pid,"

cd '/home/rkshare/programs/sn100_shell/verification/status_login_05_20261102'
sha256sum --quiet -c <<'HASHES'
8b48f0cf698cfaf8f1aeee40323e32ccc9201e2650072b217a25c34e1b1f1f05  status_login_review_05
4e502617172cdc6b749fbb4f2945343b1e6646aed20d9ef3d886620c7ec9498e  run_case_on_board.sh
a6f74ddd8b7b5b6620ef031312bcc26444119b44696a12345793f74a442a046c  capture_application_exit.sh
HASHES
test "$(cat review_release_id)" = 'e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270'
chown lckfb:lckfb status_login_review_05 review_release_id run_case_on_board.sh capture_application_exit.sh
chmod 750 status_login_review_05 run_case_on_board.sh capture_application_exit.sh
chmod 640 review_release_id
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
printf 'CURRENT_BEFORE=%s\n' "$(readlink "$base/current")"
printf 'CURRENT_OWNER=%s\n' "$(stat -c '%u:%g' "$base/current")"
printf 'CONTROLLER_PID=%s\nPROGRAM_STARTED=0\n' "$controller_pid"
date -Is
