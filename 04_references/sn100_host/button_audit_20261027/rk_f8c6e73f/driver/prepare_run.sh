#!/usr/bin/env bash
# 用户已同意临时使用屏幕；仅正常关闭当前程序，现场数据只核文件指纹。
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ssh -o BatchMode=yes -o ConnectTimeout=6 lckfb@192.168.100.123 bash -s > "$here/prepare_run.log" 2>&1 <<'REMOTE'
set -Eeuo pipefail
base=/home/rkshare/programs/sn100_shell
expected=f8c6e73f59f458981e243af5dd00999c12a671f449f0daaf3f4ff70e82d8a11f
release=$(readlink -f "$base/current")
test "$release" = "$base/releases/$expected"
test "$(cat "$base/latest_run")" = 20261027_171425_290058
test ! -e "$base/verification/button_audit_01"
(cd "$release" && sha256sum --quiet -c SHA256SUMS)
printf 'VERIFIED_RELEASE=%s\n' "$expected"
sha256sum "$release/sn100_shell"
bash "$base/sn100.sh" stop
if sudo -n fuser /dev/tty7 /dev/dri/card0 2>/dev/null; then echo DISPLAY_STILL_IN_USE; exit 4; fi
echo USER_AUTHORIZED_DISPLAY_AVAILABLE=1
REMOTE
cat "$here/prepare_run.log"
