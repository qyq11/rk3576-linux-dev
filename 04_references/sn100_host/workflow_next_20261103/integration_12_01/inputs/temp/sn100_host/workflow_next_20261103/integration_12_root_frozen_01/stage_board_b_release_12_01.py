"""下一批仅上传并保存到板B独立版本目录；不切current，不启动或停止程序。"""
from pathlib import Path, PurePosixPath
from datetime import datetime
import base64
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import tarfile
import uuid

from integration_12_01_tool_support import (
    TOOLS, ROOT, WORK, CONFIG, cfg, promotion, source_snapshot, verify_build,
    require_regular, read_json, write_new, record, sha, safe_name,
    LIVE_GUARD_FORMAT, LIVE_GUARD_KEYS, protected_configuration_bytes,
    decode_configuration_guards, configuration_guard,
)

HOST = "root@192.168.100.123"
ALIAS = "rk3576-board-b"
HOST_PIN = "SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg"
SERIAL = "8314b1e8ffd0321f"
SHA = re.compile(r"[a-f0-9]{64}\Z")
STORAGE_LOCK_PATH = "/home/rkshare/programs/sn100_shell/immutable_store.lock"
STORAGE_LOCK_KIND = "exclusive_immutable_store_only"
LIVE_GUARD_SCOPE = "locked_unpack_and_commit"


def now():
    return datetime.now().astimezone().isoformat()


def write_bytes_new(path, value):
    # 网络结果也用独占创建；先前失败的日志和退出码不能被下一次覆盖。
    path.resolve().relative_to(WORK.resolve())
    if not path.parent.is_dir():
        raise RuntimeError("结果父目录不存在：" + str(path.parent))
    with path.open("xb") as output:
        output.write(value)
        output.flush()
        os.fsync(output.fileno())


def known_host():
    name = ("C:/Users/52400/.ssh/known_hosts_rk3576_board_b" if os.name == "nt"
            else "/mnt/c/Users/52400/.ssh/known_hosts_rk3576_board_b")
    path = Path(name)
    require_regular(path, within=path.parent)
    raw = path.read_bytes()
    keys = []
    for line in raw.decode("utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = line.split()
        if len(fields) >= 3 and ALIAS in fields[0].split(","):
            if fields[1] != "ssh-ed25519":
                raise RuntimeError("板B别名存在未经本工具固定的公钥类型。")
            key = base64.b64decode(fields[2], validate=True)
            pin = "SHA256:" + base64.b64encode(hashlib.sha256(key).digest()).decode("ascii").rstrip("=")
            keys.append(pin)
    if keys != [HOST_PIN]:
        raise RuntimeError("板B专用known_hosts必须恰有一条指定别名及已核ED25519指纹。")
    return path, hashlib.sha256(raw).hexdigest()


def verify_package(package, complete, snapshot):
    # 本地只读核包：先排除链接和越界条目，再把包内实物与构建终态逐份对上。
    contents, special = {}, {}
    with tarfile.open(package, "r:gz") as bundle:
        for member in bundle:
            name = safe_name(member.name)
            if member.isdir():
                continue
            if not member.isfile() or name in contents:
                raise RuntimeError("程序包包含链接、重复或非普通文件：" + name)
            source = bundle.extractfile(member)
            if source is None:
                raise RuntimeError("程序包条目不能读取：" + name)
            digest = hashlib.sha256()
            captured = bytearray()
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
                if name in ("SHA256SUMS", "release_id", "release.json"):
                    captured.extend(block)
            contents[name] = digest.hexdigest()
            if name in ("SHA256SUMS", "release_id", "release.json"):
                special[name] = bytes(captured)
    if set(special) != {"SHA256SUMS", "release_id", "release.json"}:
        raise RuntimeError("程序包缺少清单或发布身份。")
    release = complete["release_id"]
    if (hashlib.sha256(special["SHA256SUMS"]).hexdigest() != release
            or special["release_id"].decode("ascii").strip() != release):
        raise RuntimeError("包内发布编号与清单原文指纹不符。")
    wanted = {}
    for line in special["SHA256SUMS"].decode("ascii").splitlines():
        fingerprint, name = line.split("  ", 1)
        safe_name(name)
        if not SHA.fullmatch(fingerprint) or name in wanted:
            raise RuntimeError("包内清单指纹无效或文件重复。")
        wanted[name] = fingerprint
    if {name: value for name, value in contents.items() if name not in ("SHA256SUMS", "release_id")} != wanted:
        raise RuntimeError("程序包实际文件与完整清单不同。")
    metadata = json.loads(special["release.json"].decode("utf-8"))
    if (metadata.get("sources") != snapshot
            or metadata.get("binary_sha256") != complete["binaries"]["sn100_shell"]
            or metadata.get("renderer_sha256") != complete["binaries"]["sn100_report_renderer"]
            or metadata.get("dependencies_sha256") != complete["dependency_report_sha256"]):
        raise RuntimeError("包内源码、程序或依赖身份不等于本次构建终态。")
    for name, fingerprint in complete["binaries"].items():
        if contents.get(name) != fingerprint:
            raise RuntimeError("程序包内实际程序指纹不符：" + name)


def decode_live_guard(lines, configuration):
    # 只解码并保存板端原字节；这里不在电脑伪造进程或运行观察结果。
    snapshots = []
    for phase in ("BEFORE", "AFTER"):
        values = {}
        for suffix in ("SHA256", "BASE64"):
            prefix = "LIVE_GUARD_" + phase + "_" + suffix + "="
            matches = [line[len(prefix):] for line in lines if line.startswith(prefix)]
            if len(matches) != 1:
                raise RuntimeError("板端运行保护标记缺失或重复：" + prefix)
            values[suffix] = matches[0]
        if not SHA.fullmatch(values["SHA256"]) or len(values["BASE64"]) > 32768:
            raise RuntimeError("板端运行保护标记格式或大小不符。")
        raw = base64.b64decode(values["BASE64"], validate=True)
        if (not raw.endswith(b"\n") or raw.endswith(b"\n\n") or b"\r" in raw or b"\0" in raw
                or len(raw) > 16384 or hashlib.sha256(raw).hexdigest() != values["SHA256"]):
            raise RuntimeError("板端运行保护原文或指纹不符。")
        pairs = [line.partition("=") for line in raw.decode("utf-8").splitlines()]
        if tuple(pair[0] for pair in pairs) != LIVE_GUARD_KEYS or any(pair[1] != "=" for pair in pairs):
            raise RuntimeError("板端运行保护原文不是约定字段与顺序。")
        state = {key: value for key, _, value in pairs}
        base = "/home/rkshare/programs/sn100_shell"
        if (state["FORMAT"] != LIVE_GUARD_FORMAT or state["BOARD"] != "board_b" or state["SERIAL"] != SERIAL
                or state["CURRENT_RELEASE"] != configuration["expected_current"]
                or state["MAIN_EXE"] != base + "/" + configuration["expected_current"] + "/sn100_shell"
                or state["CONTROLLER_EXE"] != configuration["expected_controller_exe"]
                or state["MAIN_EXE_SHA256"] != configuration["expected_running_application_sha256"]
                or state["CONTROLLER_EXE_SHA256"] != configuration["expected_controller_sha256"]
                or state["CONFIGURATION_SHA256"] != configuration["protected_configuration_manifest"]["sha256"]
                or not re.fullmatch(r"[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}", state["BOOT_ID"])):
            raise RuntimeError("板端运行保护板卡、版本或执行文件不等于冻结原值。")
        for key in ("MAIN_PID", "CONTROLLER_PID"):
            if not re.fullmatch(r"[1-9][0-9]*", state[key]):
                raise RuntimeError("运行保护缺少实际唯一进程身份：" + key)
        if state["MAIN_PID"] == state["CONTROLLER_PID"]:
            raise RuntimeError("主程序与原主控不能使用同一进程身份。")
        for key in ("MAIN_UID", "CONTROLLER_UID", "MAIN_START_TICKS", "CONTROLLER_START_TICKS"):
            if not re.fullmatch(r"[0-9]+", state[key]):
                raise RuntimeError("运行保护缺少实际UID或启动计数：" + key)
        endpoints = state["CONTROLLER_LISTEN_ENDPOINTS"].split(",")
        if not endpoints or any(not endpoint or not endpoint.endswith(":5000") or any(ch.isspace() for ch in endpoint) for endpoint in endpoints):
            raise RuntimeError("运行保护没有原主控实际5000监听地址。")
        for label, name in (("ORIGINAL_PROJECT_ROOT", "original_project_root"),
                            ("INSTRUMENT_CONFIGURATION_FILE", "instrument_configuration_file"),
                            ("THEME_CONFIGURATION_FILE", "theme_configuration_file")):
            if state["POINTER_" + label + "_PATH"] != base + "/" + name:
                raise RuntimeError("运行保护引用了另一资料/配置指针。")
        if any(not SHA.fullmatch(state[key]) for key in LIVE_GUARD_KEYS if key.endswith("_SHA256")):
            raise RuntimeError("运行保护缺少执行文件或指针的实际指纹。")
        snapshots.append(raw)
    if snapshots[0] != snapshots[1]:
        raise RuntimeError("持存储锁解包/提交前后运行身份或指针变化，不登记完整成功。")
    return snapshots


def main():
    configuration = cfg()  # frozen=false时在任何输出或网络动作之前拒绝。
    configuration_manifest = protected_configuration_bytes(configuration)
    if configuration.get("operation_permissions", {}).get("allow_stage") is not True:
        raise RuntimeError("根没有冻结只保存许可；未产生阶段输出或连接板卡。")
    complete = verify_build()  # 完整沿用构建终态、真实退出码、输入及源码的公共核对。
    expected_current = configuration.get("expected_current")
    if not isinstance(expected_current, str) or not re.fullmatch(r"releases/[a-f0-9]{64}", expected_current):
        raise RuntimeError("下一批冻结配置缺少明确的预期current，未猜测板端当前版本。")
    controller_exe = configuration.get("expected_controller_exe")
    if (not isinstance(controller_exe, str) or not controller_exe.startswith("/")
            or PurePosixPath(controller_exe).as_posix() != controller_exe
            or any(part in ("", ".", "..") for part in controller_exe[1:].split("/"))
            or any(ch in controller_exe for ch in "\0\r\n\t")):
        raise RuntimeError("根尚未冻结原主控实际执行文件位置；不猜路径或旧PID。")
    promotion()
    snapshot = source_snapshot()
    complete_path = WORK / "build_complete.json"
    if read_json(complete_path) != complete:
        raise RuntimeError("完整构建核对后终态已变化，未连接板卡。")
    build_exit = require_regular(WORK / "build_exit.txt").read_text(encoding="ascii").strip()
    if (build_exit != "0" or complete.get("format") != "sn100-integration-12-01-build-complete-v1"
            or complete.get("arm64_build_and_package_passed") is not True
            or complete.get("program_executed") is not False or complete.get("board_runtime_verified") is not False):
        raise RuntimeError("下一批实际构建、打包终态未通过，未连接板卡。")
    package_name = complete.get("package")
    matched = re.fullmatch(r"sn100_shell_([a-f0-9]{64})\.tar\.gz", package_name or "")
    if not matched or complete.get("release_id") != matched.group(1):
        raise RuntimeError("构建终态的包名或发布编号无效。")
    release = matched.group(1)
    remote_release = "/home/rkshare/programs/sn100_shell/releases/integration_12_01/" + release
    binaries = complete.get("binaries")
    if (not isinstance(binaries, dict) or set(binaries) != {"sn100_shell", "sn100_report_renderer"}
            or any(not isinstance(value, str) or not SHA.fullmatch(value) for value in binaries.values())
            or complete.get("source_count") != len(snapshot)):
        raise RuntimeError("构建终态缺少两个真实程序指纹或源码数量不符。")
    if (complete.get("source_snapshot_sha256") != sha(WORK / "source_snapshot.json")
            or read_json(WORK / "source_snapshot.json") != snapshot
            or complete.get("promotion_progress_sha256") != configuration["promotion_progress"]["sha256"]
            or complete.get("dependency_report_sha256") != sha(WORK / "dependencies/dependency_report.json")):
        raise RuntimeError("构建终态不属于本次实际源码、晋级或依赖报告。")
    # 固定使用完整构建声明的basename，绝不读latest_package替代本批程序包。
    package = require_regular(WORK / "packages" / package_name, within=WORK / "packages")
    package_sha = sha(package)
    if complete.get("package_sha256") != package_sha:
        raise RuntimeError("实际程序包指纹不等于完整构建终态。")
    verify_package(package, complete, snapshot)
    shell_path = TOOLS / "stage_board_b_release_12_01.sh"
    script = require_regular(shell_path).read_bytes()
    if b"\r" in script:
        raise RuntimeError("板端脚本必须保持冻结LF原文。")
    trust_path, trust_sha = known_host()
    options = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "-o", "StrictHostKeyChecking=yes",
               "-o", "HostKeyAlias=" + ALIAS, "-o", "HostKeyAlgorithms=ssh-ed25519",
               "-o", "UserKnownHostsFile=" + str(trust_path), "-o", "GlobalKnownHostsFile=" + os.devnull,
               "-o", "UpdateHostKeys=no", "-o", "VerifyHostKeyDNS=no", "-o", "CheckHostIP=no"]
    outputs = [WORK / name for name in ("stage_started.json", "stage_complete.json", "stage_upload.sh", "upload.exit", "board_b_stage.exit", "live_guard_before.txt", "live_guard_after.txt", "configuration_guard_before.sha256", "configuration_guard_after.sha256")]
    outputs += [WORK / "logs" / name for name in ("upload.log", "board_b_stage.log")]
    for path in outputs:
        if path.exists() or path.is_symlink():
            raise RuntimeError("已有本批上传或暂存记录；保留原件，不自动重跑：" + str(path))
    if not (WORK / "logs").is_dir():
        raise RuntimeError("实际构建日志目录不存在。")
    remote = "/home/rkshare/programs/sn100_shell/.workflow_upload_12_01_" + uuid.uuid4().hex + ".tar.gz"
    # 上传脚本也保存原文，collector不必凭工具名称推测当次实际发送的内容。
    upload_shell = f"""set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\\0' </proc/device-tree/serial-number)" = {SERIAL}
base=/home/rkshare/programs/sn100_shell
test "$(readlink -f "$base")" = "$base"
test "$(readlink "$base/current")" = '{expected_current}'
store_lock=$base/immutable_store.lock
test ! -L "$store_lock"
if test -e "$store_lock"; then test -f "$store_lock"; fi
exec 9>>"$store_lock"
flock -n 9
test ! -L "$store_lock"
test "$store_lock" -ef "/proc/$$/fd/9"
test "$(tr -d '\\0' </proc/device-tree/serial-number)" = {SERIAL}
test "$(readlink "$base/current")" = '{expected_current}'
archive='{remote}'
test ! -e "$archive"
test ! -L "$archive"
umask 077
set -o noclobber
cat >"$archive"
test "$(sha256sum "$archive" | cut -d' ' -f1)" = '{package_sha}'
test "$(readlink "$base/current")" = '{expected_current}'
printf 'BOARD=board_b\\nSERIAL={SERIAL}\\nPACKAGE_SHA256={package_sha}\\nUPLOAD_SCRIPT_EXIT=0\\n'
"""
    upload_path = WORK / "stage_upload.sh"
    write_bytes_new(upload_path, upload_shell.encode("utf-8"))
    inputs = {
        "config": record(CONFIG), "build_complete": record(complete_path), "build_exit": record(WORK / "build_exit.txt"),
        "build_log": record(WORK / "logs/build.log"), "source_snapshot": record(WORK / "source_snapshot.json"),
        "dependency_report": record(WORK / "dependencies/dependency_report.json"), "package": record(package),
        "stage_script": record(shell_path), "stage_tool": record(Path(__file__).resolve()), "upload_script": record(upload_path),
        "support_tool": record(TOOLS / "integration_12_01_tool_support.py"),
        "protected_configuration_manifest": dict(configuration["protected_configuration_manifest"]),
    }
    for label in ("final_review", "promotion_progress"):
        inputs[label] = dict(configuration[label])
    for group in ("patches", "evidence", "tools"):
        for index, item in enumerate(configuration[group], 1):
            inputs[group + "_" + str(index)] = dict(item)
    started = {"format": "sn100-integration-12-01-stage-started-v1", "batch": "integration_12_01", "started_at": now(),
               "board": "board_b", "serial": SERIAL, "ssh_host": HOST, "host_key_alias": ALIAS,
               "host_fingerprint": HOST_PIN, "known_hosts_sha256": trust_sha, "current_before": expected_current,
               "storage_lock_path": STORAGE_LOCK_PATH, "storage_lock_kind": STORAGE_LOCK_KIND,
               "live_guard_format": LIVE_GUARD_FORMAT, "live_guard_scope": LIVE_GUARD_SCOPE,
               "expected_controller_exe": controller_exe,
               "expected_running_application_sha256": configuration["expected_running_application_sha256"],
               "expected_controller_sha256": configuration["expected_controller_sha256"],
               "release_id": release, "package_sha256": package_sha, "remote_upload": remote, "remote_release": remote_release,
               "stage_script_sha256": inputs["stage_script"]["sha256"],
               "upload_script_sha256": inputs["upload_script"]["sha256"], "inputs": inputs, "complete": False}
    write_new(WORK / "stage_started.json", started)  # 任何网络动作之前先保存意图和本轮实际输入。
    # scp没有拒绝覆盖开关；同一已核SSH通道以noclobber排它创建，失败留下原上传现场。
    with package.open("rb") as source:
        uploaded = subprocess.run(["ssh", *options, HOST, "bash -c " + shlex.quote(upload_shell)], stdin=source,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    write_bytes_new(WORK / "logs/upload.log", uploaded.stdout)
    write_bytes_new(WORK / "upload.exit", (str(uploaded.returncode) + "\n").encode("ascii"))
    print("ACTUAL_UPLOAD_EXIT=" + str(uploaded.returncode), flush=True)
    if uploaded.returncode:
        sys.stdout.buffer.write(uploaded.stdout)
        return uploaded.returncode
    upload_lines = uploaded.stdout.decode("utf-8").splitlines()
    for marker in ("BOARD=board_b", "SERIAL=" + SERIAL, "PACKAGE_SHA256=" + package_sha, "UPLOAD_SCRIPT_EXIT=0"):
        if upload_lines.count(marker) != 1:
            raise RuntimeError("真实上传退出0，但远端上传核对标记缺失或重复：" + marker)
    if known_host()[1] != trust_sha:
        raise RuntimeError("上传后板B公钥文件已变化，未继续ssh保存；上传事实保留。")
    for item in inputs.values():
        if sha(ROOT / item["path"]) != item["sha256"]:
            raise RuntimeError("上传期间实际输入变化，未继续保存；上传及原记录保留。")
    command = "bash -s -- " + " ".join(shlex.quote(value) for value in
        (remote, package_sha, release, expected_current, controller_exe,
         base64.b64encode(configuration_manifest).decode("ascii"),
         configuration["expected_running_application_sha256"], configuration["expected_controller_sha256"]))
    staged = subprocess.run(["ssh", *options, HOST, command], input=script,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    write_bytes_new(WORK / "logs/board_b_stage.log", staged.stdout)
    write_bytes_new(WORK / "board_b_stage.exit", (str(staged.returncode) + "\n").encode("ascii"))
    sys.stdout.buffer.write(staged.stdout)
    print("ACTUAL_STAGE_EXIT=" + str(staged.returncode), flush=True)
    if staged.returncode:
        return staged.returncode
    lines = staged.stdout.decode("utf-8").splitlines()
    for marker in ("BOARD=board_b", "SERIAL=" + SERIAL, "STAGED_RELEASE=" + release,
                   "CURRENT_RELEASE=" + expected_current, "REMOTE_RELEASE=" + remote_release, "PACKAGE_SHA256=" + package_sha,
                   "STORAGE_LOCK_PATH=" + STORAGE_LOCK_PATH, "STORAGE_LOCK_KIND=" + STORAGE_LOCK_KIND,
                   "ACTIVATED=0", "PROGRAM_STARTED=0", "STAGE_BATCH=integration_12_01", "STAGE_SCRIPT_EXIT=0"):
        if lines.count(marker) != 1:
            raise RuntimeError("真实ssh退出0，但保存标记缺失或重复；未登记complete：" + marker)
    if "not found" in staged.stdout.decode("utf-8") or lines.count("DEPENDENCIES_BEGIN") != 1 or lines.count("DEPENDENCIES_END") != 1:
        raise RuntimeError("实际依赖结果缺失或存在未找到动态库，未登记complete。")
    for name, fingerprint in binaries.items():
        marker = fingerprint + "  " + remote_release + "/" + name
        if lines.count(marker) != 1:
            raise RuntimeError("板端实际程序指纹不等于构建终态：" + name)
    config_before, config_after = decode_configuration_guards(lines, configuration)
    write_bytes_new(WORK / "configuration_guard_before.sha256", config_before)
    write_bytes_new(WORK / "configuration_guard_after.sha256", config_after)
    guard_before, guard_after = decode_live_guard(lines, configuration)
    write_bytes_new(WORK / "live_guard_before.txt", guard_before)
    write_bytes_new(WORK / "live_guard_after.txt", guard_after)
    for item in inputs.values():
        if sha(ROOT / item["path"]) != item["sha256"]:
            raise RuntimeError("暂存期间实际输入变化，保留真实ssh结果，不登记完整终态。")
    if known_host()[1] != trust_sha:
        raise RuntimeError("暂存期间板B公钥文件变化，保留结果待核。")
    if verify_build() != complete:
        raise RuntimeError("暂存后的完整构建核对不再等于开始时终态，保留真实结果待核。")
    finished = {**started, "format": "sn100-integration-12-01-stage-complete-v1", "complete": True,
                "completed_at": now(), "current_after": expected_current, "actual_upload_exit": uploaded.returncode,
                "actual_stage_exit": staged.returncode, "activation_performed": False, "program_started": False,
                 "live_guard": {"format": LIVE_GUARD_FORMAT, "before": record(WORK / "live_guard_before.txt"),
                                "after": record(WORK / "live_guard_after.txt"), "unchanged": True},
                "configuration_guard": configuration_guard(configuration),
                "started_record": record(WORK / "stage_started.json"),
                "results": {name: record(WORK / name) for name in
                            ("logs/upload.log", "upload.exit", "logs/board_b_stage.log", "board_b_stage.exit", "live_guard_before.txt", "live_guard_after.txt", "configuration_guard_before.sha256", "configuration_guard_after.sha256")}}
    write_new(WORK / "stage_complete.json", finished)
    print("BOARD_B_INTEGRATION_12_01_STAGED=1\nACTIVATED=0\nPROGRAM_STARTED=0")
    return 0


if __name__ == "__main__":
    sys.exit(main())
