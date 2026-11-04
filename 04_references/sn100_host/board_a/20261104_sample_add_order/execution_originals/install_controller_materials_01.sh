#!/usr/bin/env bash
base=/home/rkshare/programs/sn100_shell
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
[[ $(readlink "$base/current") == releases/ef294a921e7eec2d293beb5aa0fbe5784d6b5056cef92648e799c3529b839a14 ]]
[[ -z $(ss -H -ltnp 'sport = :5000') ]]
[[ ! -e /userdata/shared && ! -L /userdata/shared ]]
archive=$base/.board_a_original_controller_20261104_01.tar.gz
[[ $(sha256sum "$archive" | cut -d' ' -f1) == ca280210d564808d461f183861e3ced573517a8364aeb396fa5c29c6a67a0ac6 ]]
mkdir -m 755 /userdata/shared
tar --no-same-owner --no-same-permissions -xzf "$archive" -C /userdata/shared
(cd /userdata/shared && sha256sum -c INPUT_SHA256SUMS)
chmod 755 /userdata/shared/ACA_V1.0OffLineD-500
LD_LIBRARY_PATH=/userdata/shared ldd /userdata/shared/ACA_V1.0OffLineD-500
[[ $(sha256sum /userdata/shared/ACA_V1.0OffLineD-500 | cut -d' ' -f1) == ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff ]]
printf 'OLD_THEME_BASE64='; base64 -w0 "$base/runtime/data/theme_config.ini"; printf '\n'
printf 'OLD_CHANNEL_SHA256='; sha256sum "$base/runtime/data/db/systemInfo/channel.db"
if ! command -v sqlite3 >/dev/null; then
    deb=$base/.board_a_sqlite3_20261104_01.deb
    [[ $(sha256sum "$deb" | cut -d' ' -f1) == ced8d1e467584a3d96b89b79d1b8342b173966db21f9acedae8f568ca7f85147 ]]
    dpkg -i "$deb"
fi
runuser -u lckfb -- sqlite3 --version
printf 'ORIGINAL_CHANNEL_TABLES\n'
runuser -u lckfb -- sqlite3 -readonly "$base/runtime/data/db/systemInfo/channel.db" "SELECT name FROM sqlite_master WHERE type='table';"
printf 'SUPPLIED_CHANNEL_TABLES\n'
sqlite3 -readonly /userdata/shared/Channel.db "SELECT name FROM sqlite_master WHERE type='table';"
printf 'LOGIN_AND_CONNECTION_NON_SECRET_FIELDS\n'
awk '/^\[/{section=$0} /^[[:space:]]*[^#;].*=/{if((section=="[login]" && $0 ~ /^(open|defaultPermission)[[:space:]]*=/) || (section=="[link]" && $0 ~ /^open[[:space:]]*=/) || (section=="[userOption]" && $0 ~ /^isContainerTube[[:space:]]*=/))print section,$0}' "$base/runtime/data/config.ini" "$base/runtime/data/config_Debug.ini"
printf 'CONTROLLER_MATERIALS_INSTALLED_NO_PRODUCT_STARTED=1\n'
