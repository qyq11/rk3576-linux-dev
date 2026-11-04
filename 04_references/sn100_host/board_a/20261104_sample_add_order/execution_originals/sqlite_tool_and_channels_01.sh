#!/usr/bin/env bash
base=/home/rkshare/programs/sn100_shell
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
# 上一步新装CLI因精确依赖版本未配置；撤回这份新包，保留原系统库。
dpkg-query -W -f='SQLITE_PACKAGE=${db:Status-Abbrev} ${Version}\n' sqlite3
dpkg --remove sqlite3
toolroot=$base/.board_a_sqlite_tool_20261104_01
[[ ! -e $toolroot && ! -L $toolroot ]]
mkdir -m 700 "$toolroot"
dpkg-deb -x "$base/.board_a_sqlite3_20261104_01.deb" "$toolroot"
[[ ! -e /usr/local/bin/sqlite3 && ! -L /usr/local/bin/sqlite3 ]]
install -m 755 "$toolroot/usr/bin/sqlite3" /usr/local/bin/sqlite3
ldd /usr/local/bin/sqlite3
runuser -u lckfb -- /usr/local/bin/sqlite3 --version
dpkg-query -W -f='SYSTEM_SQLITE_LIBRARY=${Version}\n' libsqlite3-0:arm64
printf 'ORIGINAL_CHANNEL_TABLES\n'
runuser -u lckfb -- sqlite3 -readonly "$base/runtime/data/db/systemInfo/channel.db" "SELECT name FROM sqlite_master WHERE type='table';"
printf 'SUPPLIED_CHANNEL_TABLES\n'
sqlite3 -readonly /userdata/shared/Channel.db "SELECT name FROM sqlite_master WHERE type='table';"
printf 'LOGIN_AND_CONNECTION_NON_SECRET_FIELDS\n'
awk '/^\[/{section=$0} /^[[:space:]]*[^#;].*=/{if((section=="[login]" && $0 ~ /^(open|defaultPermission)[[:space:]]*=/) || (section=="[link]" && $0 ~ /^open[[:space:]]*=/) || (section=="[userOption]" && $0 ~ /^isContainerTube[[:space:]]*=/))print section,$0}' "$base/runtime/data/config.ini" "$base/runtime/data/config_Debug.ini"
printf 'SQLITE_TOOL_READY_ORIGINAL_SYSTEM_LIBRARY_PRESERVED=1\n'
