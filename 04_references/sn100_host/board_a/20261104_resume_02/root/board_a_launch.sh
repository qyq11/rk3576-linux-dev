cd /
base=/home/rkshare/programs/sn100_shell
release=/home/rkshare/programs/sn100_shell/releases/87768ca46fef52bc6323ff6a24eb920ff7bb1c0344279e12881b0aa44e2ea3b8
verification=/home/rkshare/programs/sn100_shell/verification/resume_20261104_02
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
[[ $(readlink /proc/4426/exe) == /userdata/shared/ACA_V1.0OffLineD-500 ]]
[[ $(awk '{print $22}' /proc/4426/stat) == 196590 ]]
[[ $(ss -H -ltnp 'sport = :5000') == *'pid=4426,'* ]]
[[ $(readlink -f "$base/current") == /home/rkshare/programs/sn100_shell/releases/0b0b93787b055f51bba1ac579e0a79488959940284e0a702ab2fa06a1934e743 ]]
[[ $(sha256sum "/home/rkshare/programs/sn100_shell/runtime/data/calibrationFile/ReagentAmountCalibration.ini" | cut -d" " -f1) == ee0bfe46adef3d4e39dd975e3b3f1a77cbc6925a46284f8e5af7ae452a302175 ]]
[[ $(sha256sum "/home/rkshare/programs/sn100_shell/runtime/data/config_Debug.ini" | cut -d" " -f1) == 58374e000baaa65c58bb4d3cf1bb84458b4a4de7348e4f54371f2e957fa4b963 ]]
[[ $(sha256sum "/home/rkshare/programs/sn100_shell/runtime/data/config.ini" | cut -d" " -f1) == 403348a29b6dafddf4f0511dfd4b8f33277bd56a30b7408e1b5f6e2f54de078e ]]
[[ $(sha256sum "/home/rkshare/programs/sn100_shell/runtime/data/hospital/config.ini" | cut -d" " -f1) == e8fb4cd6665a2adfb2fdb586063d46c03342e9400c6c675081bbcacb2d67dd55 ]]
[[ $(sha256sum "/home/rkshare/programs/sn100_shell/runtime/data/settings/lis.ini" | cut -d" " -f1) == fca544425e1ce154ae5cf9e22094c1482a98885f1dae9a3ed8b16588010aa768 ]]
[[ $(sha256sum "/home/rkshare/programs/sn100_shell/runtime/data/settings/printer.ini" | cut -d" " -f1) == 4ad744292757d7f7e6b1aced710a4a5819384c9c553ee8835cc7911c5c74a5c2 ]]
[[ $(sha256sum "/home/rkshare/programs/sn100_shell/runtime/data/theme_config.ini" | cut -d" " -f1) == 029605f35924de646b5402f1e377c93cb26e01952e0daa5fda300fe47f7feed7 ]]
[[ $(sha256sum /home/rkshare/programs/sn100_shell/incoming/resume_20261104_02_87768ca46fef52bc6323ff6a24eb920ff7bb1c0344279e12881b0aa44e2ea3b8.tar.gz | cut -d' ' -f1) == 18334618311918f2cd4d90a39f6a2912bd6385bb5769c22add14b4c1c850fd8c ]]
[[ $(sha256sum "$verification/live_ui_observer.so" | cut -d' ' -f1) == 250c35c0146871f21f0daeb2a215ec75bac4d26e42276f8dc2a8b5e80b160452 ]]
[[ $(sha256sum "$verification/live_supervisor.sh" | cut -d' ' -f1) == cd605f45d48d92a77798fadb8f6b6f90a9476e916252f09bb91f60af70653e00 ]]
[[ ! -L "$base/lifecycle.lock" ]]
exec 8>> "$base/lifecycle.lock"
flock -n 8
for entry in /proc/[0-9]*/exe; do
 exe=$(readlink "$entry" 2>/dev/null) || continue
 [[ ${exe##*/} != sn100_shell && ${exe##*/} != sample_add_review ]]
done
[[ ! -e $release && ! -L $release && ! -L "$base/releases" ]]
stage=$(mktemp -d "$base/releases/.resume_20261104_02.XXXXXXXX")
install -d -m 700 "$stage/87768ca46fef52bc6323ff6a24eb920ff7bb1c0344279e12881b0aa44e2ea3b8"
tar --no-same-owner --no-same-permissions -xzf /home/rkshare/programs/sn100_shell/incoming/resume_20261104_02_87768ca46fef52bc6323ff6a24eb920ff7bb1c0344279e12881b0aa44e2ea3b8.tar.gz -C "$stage/87768ca46fef52bc6323ff6a24eb920ff7bb1c0344279e12881b0aa44e2ea3b8"
source "$stage/87768ca46fef52bc6323ff6a24eb920ff7bb1c0344279e12881b0aa44e2ea3b8/rk_board.sh"
verify_release "$stage/87768ca46fef52bc6323ff6a24eb920ff7bb1c0344279e12881b0aa44e2ea3b8"
mv -T "$stage/87768ca46fef52bc6323ff6a24eb920ff7bb1c0344279e12881b0aa44e2ea3b8" "$release"
rmdir "$stage"
chown -R lckfb:lckfb "$release"
chown lckfb:lckfb "$verification/live_ui_observer.so" "$verification/live_supervisor.sh"
chmod 600 "$verification/live_ui_observer.so"
chmod 700 "$verification/live_supervisor.sh"
for object in "$release/sn100_shell" "$release/sn100_report_renderer" "$verification/live_ui_observer.so"; do
 LD_LIBRARY_PATH="$release/lib" ldd "$object"
done > "$verification/dependencies.txt"
! grep -q 'not found' "$verification/dependencies.txt"
runuser -u lckfb -- bash -c 'source "$1"; preflight "$2" "$3"' board-a-preflight "$release/rk_board.sh" "$base" "$release"
run=$base/runs/$(date +%Y%m%d_%H%M%S)_resume02_$$
[[ ! -e $run && ! -L $run ]]
install -d -o lckfb -g lckfb -m 700 "$run"
printf 'RUN_DIRECTORY=%s
RELEASE_ID=%s
CURRENT_UNCHANGED=1
' "$run" '87768ca46fef52bc6323ff6a24eb920ff7bb1c0344279e12881b0aa44e2ea3b8'
exec 8>&-
systemd-run --quiet --collect --unit=sn100-resume-20261104-02 --property=Type=exec --property=Restart=no  /usr/bin/openvt -c 7 -s -w -- /usr/sbin/runuser -u lckfb -- /bin/bash "$verification/live_supervisor.sh" "$base" "$release" "$run" "$verification"
for ((i=0;i<75;i++)); do
 if [[ -f $run/app.pid && -e /proc/$(cat "$run/app.pid")/exe ]]; then
  [[ $(sha256sum /proc/$(cat "$run/app.pid")/exe | cut -d' ' -f1) == 4bffa0b3c4d0b6928b1555ee62550cdddbdfe0a000babd79d0542be4b6548328 ]]
  printf 'APP_PID=%s
APP_SHA256=%s
' "$(cat "$run/app.pid")" '4bffa0b3c4d0b6928b1555ee62550cdddbdfe0a000babd79d0542be4b6548328'
  exit 0
 fi
 if [[ -f $run/exit_code || -f $run/startup_failure.txt ]]; then
  tail -20 "$run/supervisor.log"
  exit 2
 fi
 sleep .2
done
exit 3
