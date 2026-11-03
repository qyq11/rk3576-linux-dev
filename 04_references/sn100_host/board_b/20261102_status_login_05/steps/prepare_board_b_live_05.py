"""上传同版检查器，先保存实际旧指针，再原子切到05；不启动任何产品。"""
from pathlib import Path
from datetime import datetime
import hashlib
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
task = Path(__file__).resolve().parent
work = task / "integration_05_live"
work.mkdir(exist_ok=True)
prepared = root / "temp/sn100_host/board_b/20261102_status_login/integration_05_review_preparation"
release = "e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270"
old = "7c4abe3062ead1f5b6fc6fd495b708be5f11436504bba5e900b6ccb2c4f5d5f9"
app_sha = "609aa4b9a60109df4df241181e4622626ab9b77fba2b272ca0643a5bcf5506dc"
expected = {
    "status_login_review_05": "8b48f0cf698cfaf8f1aeee40323e32ccc9201e2650072b217a25c34e1b1f1f05",
    "run_case_on_board.sh": "4e502617172cdc6b749fbb4f2945343b1e6646aed20d9ef3d886620c7ec9498e",
    "capture_application_exit.sh": "a6f74ddd8b7b5b6620ef031312bcc26444119b44696a12345793f74a442a046c",
}
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
for name, digest in expected.items():
    if sha(prepared / name) != digest:
        raise RuntimeError("运行输入不是最终固定版本：" + name)
if (prepared / "review_release_id").read_text().strip() != release:
    raise RuntimeError("检查器发布编号不符。")
if sha(prepared / "status_login_review_05.cpp") != "73d54a7802463d41c820dfbfd914612e2e78070b9f237ced60a2ec7bf8c8f11d":
    raise RuntimeError("检查器源码已变化。")
remote = "/home/rkshare/programs/sn100_shell/verification/status_login_05_20261102"
options = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "-o", "StrictHostKeyChecking=yes",
           "-o", "HostKeyAlias=rk3576-board-b", "-o", "UserKnownHostsFile=C:/Users/52400/.ssh/known_hosts_rk3576_board_b"]
guard = "set -Eeuo pipefail\ntest \"$(tr -d '\\0' </proc/device-tree/serial-number)\" = 8314b1e8ffd0321f\n"

def run(name, body):
    script = guard + body
    (work / (name + ".sh")).write_text(script, encoding="utf-8", newline="\n")
    result = subprocess.run(["ssh", *options, "root@192.168.3.59", "bash -s"], input=script.encode(),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (work / (name + ".log")).write_bytes(result.stdout)
    (work / (name + ".exit")).write_text(str(result.returncode) + "\n", encoding="ascii")
    sys.stdout.buffer.write(result.stdout)
    if result.returncode:
        raise RuntimeError("板B步骤未通过：" + name)
    return result.stdout.decode("utf-8")

run("upload_preflight", f"test ! -e '{remote}'\ninstall -d -m 750 -o lckfb -g lckfb '{remote}'\n")
files = [prepared / name for name in (*expected, "review_release_id")]
transfer = subprocess.run(["scp", "-B", *options, *map(str, files), "root@192.168.3.59:" + remote + "/"],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(work / "upload.log").write_bytes(transfer.stdout)
if transfer.returncode:
    raise RuntimeError("检查器上传失败，未切换current。")
checks = "\n".join(digest + "  " + name for name, digest in expected.items())
common = f"""
base=/home/rkshare/programs/sn100_shell
new="$base/releases/{release}"
exec 9>"$base/lifecycle.lock"
flock -n 9
test -L "$base/current"
test "$(readlink "$base/current")" = 'releases/{old}'
test "$(sha256sum "$new/SHA256SUMS" | cut -d' ' -f1)" = '{release}'
(cd "$new" && sha256sum --quiet -c SHA256SUMS)
test "$(sha256sum "$new/sn100_shell" | cut -d' ' -f1)" = '{app_sha}'
controller_count=0
controller_pid=
for exe in /proc/[0-9]*/exe; do
    path=$(readlink "$exe" || true)
    case "$path" in
        */sn100_shell|*/status_login*review*) printf 'APP_ALREADY_RUNNING=%s\\n' "$exe"; exit 3 ;;
        /userdata/shared/ACA_V1.0OffLineD-500) controller_count=$((controller_count+1)); controller_pid=${{exe#/proc/}}; controller_pid=${{controller_pid%/exe}} ;;
    esac
done
test "$controller_count" = 1
test "$(sha256sum /proc/$controller_pid/exe | cut -d' ' -f1)" = ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff
ss -H -ltnp 'sport = :5000' | grep -q "pid=$controller_pid,"
"""
before = run("activation_preflight", common + f"""
cd '{remote}'
sha256sum --quiet -c <<'HASHES'
{checks}
HASHES
test "$(cat review_release_id)" = '{release}'
chown lckfb:lckfb status_login_review_05 review_release_id run_case_on_board.sh capture_application_exit.sh
chmod 750 status_login_review_05 run_case_on_board.sh capture_application_exit.sh
chmod 640 review_release_id
printf 'BOARD=board_b\\nSERIAL=8314b1e8ffd0321f\\n'
printf 'CURRENT_BEFORE=%s\\n' "$(readlink "$base/current")"
printf 'CURRENT_OWNER=%s\\n' "$(stat -c '%u:%g' "$base/current")"
printf 'CONTROLLER_PID=%s\\nPROGRAM_STARTED=0\\n' "$controller_pid"
date -Is
""")
values = dict(line.split("=", 1) for line in before.splitlines() if "=" in line)
backup = root / "07_backups/sn100_host/board_b/status_login_05_20261102"
backup.mkdir(exist_ok=False)
record = {"captured_at": datetime.now().astimezone().isoformat(), "board": "board_b", "serial": values["SERIAL"],
          "current_before": values["CURRENT_BEFORE"], "current_owner": values["CURRENT_OWNER"],
          "intended_current": "releases/" + release, "scope": "仅保留程序current符号链接的原目标及属主，原配置和业务库未覆盖。"}
(backup / "current_before.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
(backup / "README.md").write_text("# 板B第五轮联机检查的程序指针恢复输入\n\n切换前已实际核对并保留current旧目标与属主，详见current_before.json。只恢复程序选择，不包含业务库、账户或整机环境；原版本程序仍在同板releases目录，匹配程序和源码沿原7c归档。切回前先确认板卡及应用已正常退出，再按原目标建立同属主符号链接并原子替换current。这里不自动回滚实际报警或工作资料。\n", encoding="utf-8")
rows = [sha(p) + "  " + p.name for p in sorted(backup.iterdir()) if p.is_file()]
(backup / "SHA256SUMS").write_text("\n".join(rows) + "\n", encoding="ascii")
run("activation", common + f"""
temporary="$base/.current-05-$$"
test ! -e "$temporary" && test ! -L "$temporary"
ln -s 'releases/{release}' "$temporary"
chown -h '{values['CURRENT_OWNER']}' "$temporary"
test "$(readlink "$base/current")" = 'releases/{old}'
mv -Tf -- "$temporary" "$base/current"
printf 'BOARD=board_b\\nSERIAL=8314b1e8ffd0321f\\n'
printf 'CURRENT_AFTER=%s\\n' "$(readlink "$base/current")"
printf 'ACTIVATED=1\\nPROGRAM_STARTED=0\\nCONTROLLER_RESTARTED=0\\n'
date -Is
""")
print("LIVE_REVIEW_FILES_UPLOADED=4\nORIGINAL_POINTER_SAVED=1\nPROGRAM_STARTED=0")
