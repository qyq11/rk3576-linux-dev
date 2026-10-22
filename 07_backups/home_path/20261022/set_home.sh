#!/usr/bin/env bash
# Change only lckfb's home field under vipw's password-database lock.
set -Eeuo pipefail
export LC_ALL=C
umask 077
[[ $(id -u) == 0 ]] || { printf 'Run with sudo.\n' >&2; exit 1; }
new_home=${1:?absolute home path required}
case $new_home in
    /home/rkshare/lckfb) old_home=/home/lckfb ;;
    /home/lckfb) old_home=/home/rkshare/lckfb ;;
    *) printf 'Unexpected target home.\n' >&2; exit 2 ;;
esac
test -d /home/rkshare/lckfb
test ! -L /home/rkshare/lckfb
test "$(stat -c %u /home/rkshare/lckfb)" = 1000
expected_old="lckfb:x:1000:1000:lckfb,,,:$old_home:/bin/bash"
expected_new="lckfb:x:1000:1000:lckfb,,,:$new_home:/bin/bash"
account=$(getent passwd lckfb)
if [[ $account == "$expected_new" ]]; then
    printf '[already_configured]\n%s\n' "$account"
    exit 0
fi
test "$account" = "$expected_old"
test -L /home/lckfb
test "$(readlink -- /home/lckfb)" = /home/rkshare/lckfb
editor_dir=$(mktemp -d /run/lckfb-home.XXXXXXXX)
cleanup() {
    rm -f -- "$editor_dir/editor.sh"
    rmdir -- "$editor_dir"
}
trap cleanup EXIT
cat > "$editor_dir/editor.sh" <<'EDITOR'
#!/usr/bin/env bash
set -Eeuo pipefail
umask 077
test "$#" = 1
test -f "$1"
staged=$(mktemp "${1}.home.XXXXXXXX")
trap 'rm -f -- "$staged"' EXIT
awk -F: 'BEGIN { OFS=":"; changed=0 }
    $1 == "lckfb" {
        if (NF != 7 || $0 != ENVIRON["RK_EXPECTED_ACCOUNT"]) exit 20
        $6=ENVIRON["RK_NEW_HOME"]
        changed++
    }
    { print }
    END { if (changed != 1) exit 21 }
' "$1" > "$staged"
cat -- "$staged" > "$1"
EDITOR
chmod 700 -- "$editor_dir/editor.sh"
export RK_EXPECTED_ACCOUNT="$expected_old" RK_NEW_HOME="$new_home"
export EDITOR="$editor_dir/editor.sh" VISUAL="$editor_dir/editor.sh"
printf '[account_before]\n%s\n' "$account"
vipw --passwd --quiet </dev/null
test "$(getent passwd lckfb)" = "$expected_new"
printf '[account_after]\n'
getent passwd lckfb
