#!/usr/bin/env bash
# 只把板A的rkshare共享登录账号改为root；密码由标准输入提供，不写入文件。
set -Eeuo pipefail
export LC_ALL=C
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
test "$(sha256sum /etc/samba/smb.conf | cut -d' ' -f1)" = 012ae2e409e2f3f388e5e38d1c36bb3d28184c887a2056e628b7b1757f04bbe0
test -z "$(pdbedit -L | awk -F: '$1=="root" {print $1}')"
shadow_before=$(sha256sum /etc/shadow)
lckfb_before=$(pdbedit -L -w -u lckfb | sha256sum)
work_dir=$(mktemp -d /run/board-a-samba-root.XXXXXX)
cp -p /etc/samba/smb.conf "$work_dir/smb.conf.before"
candidate=$(mktemp /etc/samba/.smb.conf.root.XXXXXX)
root_added=0
config_changed=0
finish() {
    status=$?
    trap - EXIT
    if ((status != 0)); then
        if ((config_changed)); then
            cp -p "$work_dir/smb.conf.before" /etc/samba/smb.conf
            systemctl restart smbd || true
        fi
        if ((root_added)); then pdbedit -x -u root || true; fi
        printf 'CONFIGURATION_FAILED_AND_ROLLED_BACK=%s\n' "$status"
    fi
    rm -f -- "$candidate" "$work_dir/smb.conf.before"
    rmdir -- "$work_dir"
    exit "$status"
}
trap finish EXIT
awk '
    /^\[/ {in_share=($0=="[rkshare]")}
    in_share && /^[ \t]*valid users[ \t]*=/ {
        if ($0 !~ /^[ \t]*valid users[ \t]*=[ \t]*lckfb[ \t]*$/) exit 41
        sub(/lckfb/, "root"); changed++
    }
    {print}
    END {if(changed!=1) exit 42}
' /etc/samba/smb.conf > "$candidate"
chmod --reference=/etc/samba/smb.conf "$candidate"
chown --reference=/etc/samba/smb.conf "$candidate"
testparm -s "$candidate" >/dev/null
IFS= read -r samba_password
test -n "$samba_password"
printf '%s\n%s\n' "$samba_password" "$samba_password" |
    pdbedit -a -t -u root --option='unix password sync=no' --option='pam password change=no'
root_added=1
unset samba_password
test "$(sha256sum /etc/shadow)" = "$shadow_before"
test "$(pdbedit -L -w -u lckfb | sha256sum)" = "$lckfb_before"
config_changed=1
mv -f -- "$candidate" /etc/samba/smb.conf
systemctl restart smbd
systemctl is-active --quiet smbd
test "$(testparm -s --section-name=rkshare --parameter-name='valid users' 2>/dev/null)" = root
test "$(testparm -s --section-name=rkshare --parameter-name=path 2>/dev/null)" = /home/rkshare
test "$(sha256sum /etc/shadow)" = "$shadow_before"
test "$(pdbedit -L -w -u lckfb | sha256sum)" = "$lckfb_before"
printf 'board=board_a\nserial=3bf8222fe1417f6a\n'
date -Is
printf 'SYSTEM_PASSWORDS_UNCHANGED=PASS\nOLD_SAMBA_ACCOUNT_UNCHANGED=PASS\n'
printf 'SHARE_LOGIN_ACCOUNT=root\nSHARE_PATH=/home/rkshare\n'
sha256sum /etc/samba/smb.conf
systemctl is-active smbd
pdbedit -L
testparm -s
printf 'CONFIGURATION=PASS\n'
