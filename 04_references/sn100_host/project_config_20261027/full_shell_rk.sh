#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
host=lckfb@192.168.100.123
opts=(-o BatchMode=yes -o ConnectTimeout=8)
out=$here/full_shell_rk
mkdir -p "$out"
ssh "${opts[@]}" "$host" bash -s <<'REMOTE' > "$out/lifecycle.log" 2>&1
set -Eeuo pipefail
main=/home/rkshare/programs/sn100_shell
base=$main/verification/project_config_integrated_01
old=$main/releases/e230d2771c614ccc6f0a9ba4fe345cc632e7ddc1aaa56630a4a8f0036c5499d1
new=$(readlink -f "$main/current")
test ! -e "$base"
test "$new" != "$old"
mkdir -p "$base/releases"
# 仅复制不可变应用文件，不接触现场runtime；共享硬链接内容也不写入。
cp -al "$old" "$base/releases/${old##*/}"
cp -al "$new" "$base/releases/${new##*/}"
ln -s "releases/${old##*/}" "$base/current"
bash "$old/rk_board.sh" start "$base" 0 offlineAdmin
sleep 8
bash "$old/rk_board.sh" stop "$base"
cat "$base/latest_run" > "$base/old_anonymous_run"
ln -s "releases/${new##*/}" "$base/current.new"
mv -Tf "$base/current.new" "$base/current"
bash "$new/rk_board.sh" start "$base" 0 offlineAdmin
sleep 8
bash "$new/rk_board.sh" stop "$base"
cat "$base/latest_run" > "$base/new_migration_run"
bash "$new/rk_board.sh" start "$base" 0 offlineAdmin
sleep 8
bash "$new/rk_board.sh" stop "$base"
cat "$base/latest_run" > "$base/new_reopen_run"
printf 'FULL_SHELL_SHORT_LIFECYCLE_PASSED=1\n'
REMOTE
for stage in old_anonymous new_migration new_reopen; do
    run=$(ssh "${opts[@]}" "$host" "cat /home/rkshare/programs/sn100_shell/verification/project_config_integrated_01/${stage}_run")
    [[ $run =~ ^[0-9_]+$ ]] || exit 3
    mkdir -p "$out/$stage"
    for name in app.log runtime.txt resources.tsv result.txt exit_code; do
        scp -B "${opts[@]}" "$host:/home/rkshare/programs/sn100_shell/verification/project_config_integrated_01/runs/$run/$name" "$out/$stage/$name"
    done
done
