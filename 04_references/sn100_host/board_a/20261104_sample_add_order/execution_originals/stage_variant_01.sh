#!/usr/bin/env bash
base=/home/rkshare/programs/sn100_shell
release_id=0b0b93787b055f51bba1ac579e0a79488959940284e0a702ab2fa06a1934e743
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
[[ $(readlink "$base/current") == releases/ef294a921e7eec2d293beb5aa0fbe5784d6b5056cef92648e799c3529b839a14 ]]
[[ $(readlink /proc/4426/exe) == /userdata/shared/ACA_V1.0OffLineD-500 ]]
[[ $(awk '{print $22}' /proc/4426/stat) == 196590 ]]
[[ $(sha256sum /proc/4426/exe | cut -d' ' -f1) == ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff ]]
archive=$base/.board_a_variant_20261104_01.tar.gz
[[ $(sha256sum "$archive" | cut -d' ' -f1) == 8d664c2f5cdb50f9f62e5d680612515aefa24d53f15121940e6ac7154b36f735 ]]
[[ ! -L $base/immutable_store.lock ]]
exec 8>> "$base/immutable_store.lock"
flock -n 8
release=$base/releases/$release_id
[[ ! -e $release && ! -L $release ]]
mkdir -m 750 "$release"
tar --no-same-owner --no-same-permissions -xzf "$archive" -C "$release"
source "$release/rk_board.sh"
verify_release "$release"
[[ $(find "$release" -type f -printf '.\n' | wc -l) == 36 ]]
[[ $(sha256sum "$release/sn100_shell" | cut -d' ' -f1) == fd966beae0563c4db798b6be6c0aaa591747f375e23a1259498ab9d362d9baef ]]
chown -R lckfb:lckfb "$release"
runuser -u lckfb -- bash -c 'source "$1"; preflight "$2" "$3"' board-a-preflight "$release/rk_board.sh" "$base" "$release"
for object in sn100_shell sn100_report_renderer lib/libQt5Test.so.5; do
    LD_LIBRARY_PATH="$release/lib" ldd "$release/$object"
done
[[ $(readlink "$base/current") == releases/ef294a921e7eec2d293beb5aa0fbe5784d6b5056cef92648e799c3529b839a14 ]]
[[ $(sha256sum "$base/runtime/data/config.ini" | cut -d' ' -f1) == 403348a29b6dafddf4f0511dfd4b8f33277bd56a30b7408e1b5f6e2f54de078e ]]
[[ $(sha256sum "$base/runtime/data/theme_config.ini" | cut -d' ' -f1) == 029605f35924de646b5402f1e377c93cb26e01952e0daa5fda300fe47f7feed7 ]]
printf 'BOARD_A_VARIANT_STAGED=%s\nPROGRAM_STARTED=0\nCURRENT_CHANGED=0\n' "$release_id"
