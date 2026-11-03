"""只补第08批第6片：继承已完成五片，不重跑正式旧片，失败保留现场。"""
from pathlib import Path, PurePosixPath
import argparse
import datetime
import hashlib
import json
import os
import re
import stat
import subprocess
import sys
import uuid

PREFIX = "06_programs/sn100_host/"
FIX_TARGET = PREFIX + "shell/build.sh"
BASE_SHA = "fd5614ad0673ab6896eeea1f7502ac09ec8a4ac4f98a338f5967a4ad3717fdaf"
ARCHIVE_SHA = "18eaa7fb091d82cddee883c52ac8d6d50d2d86d9d65cd6f25cff99cd74520b41"
ATTR_SHA = "ac7fd8f4c639db19a3f6192a1cf4205bcc4ef06a573a29a2a98653fd19ae3bf8"
PARENT_PROGRESS_SHA = "95b5c41fe17c4220ca75558e1329a36d5f9ceab65c52c34e6999822a30164d5c"
PARENT_SCRIPT_SHA = "08758e3f7513367d2fc10b08c0ba6becf0d49028a59ddfd242d75d0f43aadbb9"
PARENT_REVIEW_SHA = "e2a7e04555b37b12d5be0c205aca37d3c5eddf7bca7e962848f471574848087f"
GIT = ["/usr/bin/git", "-c", "core.autocrlf=false", "-c", "core.safecrlf=false", "-c", "core.whitespace=cr-at-eol"]
GIT_CONFIG = {"implementation": GIT[0], "flags": GIT[1:], "core_eol": "unset", "wsl_distribution": "Ubuntu-24.04", "user": "qyq"}
SELF = Path(os.path.abspath(__file__))
REVIEW_DIR = SELF.parent
TASK = REVIEW_DIR.parent
ROOT = TASK.parents[2]
REVIEW_FILE = REVIEW_DIR / "final_merge_review.json"
PROGRESS_FILE = REVIEW_DIR / "fix_promotion_progress.json"
LOG_DIR = REVIEW_DIR / "fix_promotion_commands"
WSL = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32/wsl.exe"
REPARSE = 0x400
command_number = 0
progress = None
review_sha = None
own_sha = None


def sha(data):
    return hashlib.sha256(data).hexdigest()


def must(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha_value(value):
    return isinstance(value, str) and re.fullmatch(r"[a-f0-9]{64}", value) is not None


def relative(name):
    must(isinstance(name, str) and bool(name) and "\\" not in name and ":" not in name
         and '"' not in name and not any(ord(c) < 32 for c in name)
         and not any(c in name for c in "*?<>|")
         and not PurePosixPath(name).is_absolute(), "不是安全的项目相对路径：" + str(name))
    for part in name.split("/"):
        must(part not in ("", ".", "..") and not part.endswith((".", " "))
             and not re.fullmatch(r"(?i)(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", part),
             "Windows路径分量不能明确定位：" + name)
    return name


def checked(path, kind=None, missing=False):
    """检查到磁盘根的全部祖先；文件还必须只有一个硬链接。"""
    path = Path(os.path.abspath(path))
    must(path.drive.upper() == ROOT.drive.upper() and not str(path).startswith("\\\\"), "路径不在项目同一原生磁盘")
    path.relative_to(ROOT)
    ancestors = list(reversed(path.parents)) + [path]
    absent = False
    for entry in ancestors:
        try:
            info = entry.lstat()
        except FileNotFoundError:
            must(missing, "路径不存在：" + str(entry))
            absent = True
            continue
        must(not absent, "路径祖先检查不连续")
        must(not stat.S_ISLNK(info.st_mode) and not (getattr(info, "st_file_attributes", 0) & REPARSE)
             and not entry.is_symlink() and not (hasattr(entry, "is_junction") and entry.is_junction()),
             "路径经过链接或Windows重解析点：" + str(entry))
        if entry != path:
            must(stat.S_ISDIR(info.st_mode), "祖先不是目录：" + str(entry))
        elif stat.S_ISREG(info.st_mode):
            must(info.st_nlink == 1, "文件不是单链接：" + str(entry))
        elif kind != "dir":
            must(stat.S_ISDIR(info.st_mode), "不是普通文件或目录：" + str(entry))
    if not absent and kind:
        info = path.lstat()
        must(stat.S_ISDIR(info.st_mode) if kind == "dir" else stat.S_ISREG(info.st_mode), "路径类型不符：" + str(path))
    return path


def read(path):
    path = checked(path, "file")
    before = path.lstat()
    with path.open("rb") as handle:
        opened = os.fstat(handle.fileno())
        must((before.st_dev, before.st_ino) == (opened.st_dev, opened.st_ino) and opened.st_nlink == 1,
             "读取时文件身份改变：" + str(path))
        data = handle.read()
        after_open = os.fstat(handle.fileno())
    checked(path, "file")
    after = path.lstat()
    must((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) ==
         (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns) and
         (opened.st_size, opened.st_mtime_ns) == (after_open.st_size, after_open.st_mtime_ns),
         "读取前后文件改变：" + str(path))
    return data


def ref_path(name):
    return checked(ROOT / relative(name), "file")


def reference(entry):
    must(isinstance(entry, dict) and sha_value(entry.get("sha256")), "固定引用缺少真实SHA")
    path = ref_path(entry["path"])
    data = read(path)
    must(sha(data) == entry["sha256"], "固定引用改变：" + entry["path"])
    return data


def json_ref(entry):
    return json.loads(reference(entry))


def source_path(container, name, missing=False):
    relative(name)
    must(name.startswith(PREFIX), "源码目标越界：" + name)
    return checked(container / name, "file", missing=missing)


def verify_sources(container, expected):
    program = checked(container / PREFIX.rstrip("/"), "dir")
    if container != ROOT:
        actual = set()
        for current, directories, files in os.walk(program, followlinks=False):
            checked(Path(current), "dir")
            for name in directories:
                checked(Path(current) / name, "dir")
            for name in files:
                path = checked(Path(current) / name, "file")
                actual.add(path.relative_to(container).as_posix())
        present = {name for name, value in expected.items() if value is not None}
        must(actual == present, "隔离源码文件集合不等：" + str(container))
    # 正式工程还有表外原件；不遍历、复制或将它们误当选定674表的新增项。
    for name, wanted in expected.items():
        path = source_path(container, name, missing=wanted is None)
        if wanted is None:
            must(not path.exists(), "应不存在的源文件已存在：" + name)
        else:
            must(sha(read(path)) == wanted, "完整源码SHA不等：" + name)


def compare_sources(left, right, expected):
    verify_sources(left, expected)
    verify_sources(right, expected)
    for name, wanted in expected.items():
        if wanted is not None:
            must(read(source_path(left, name)) == read(source_path(right, name)), "完整源码逐字节不等：" + name)


def write_new(path, data):
    path = checked(path, "file", missing=True)
    must(not path.exists(), "拒绝覆盖既有输出：" + str(path))
    checked(path.parent, "dir")
    with path.open("xb") as handle:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())
    must(read(path) == data, "新输出读回不等：" + str(path))


def make_dir(path):
    checked(path, "dir", missing=True)
    path.mkdir(exist_ok=False)
    checked(path, "dir")


def wsl_path(path):
    path = checked(path, "dir")
    return "/mnt/" + path.drive[0].lower() + "/" + path.as_posix()[3:]


def wsl_command(cwd, command):
    return [str(WSL), "--distribution", "Ubuntu-24.04", "--user", "qyq", "--cd", wsl_path(cwd),
            "--exec", "/usr/bin/env", "-i", "HOME=/home/qyq", "USER=qyq", "LOGNAME=qyq", "PATH=/usr/bin:/bin", "LC_ALL=C", *command]


def git(cwd, args, data=b"", allowed=(0,)):
    global command_number
    command_number += 1
    tag = "%03d" % command_number
    argv = wsl_command(cwd, GIT + args)
    record = {"argv": argv, "cwd_windows": str(cwd), "cwd_wsl": wsl_path(cwd),
              "stdin_sha256": sha(data), "stdin_bytes": len(data), "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()}
    write_new(LOG_DIR / (tag + ".command.json"), (json.dumps(record, indent=2) + "\n").encode())
    result = subprocess.run(argv, input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    write_new(LOG_DIR / (tag + ".stdout.log"), result.stdout)
    write_new(LOG_DIR / (tag + ".stderr.log"), result.stderr)
    write_new(LOG_DIR / (tag + ".exit"), (str(result.returncode) + "\n").encode())
    must(result.returncode in allowed, "固定WSL Git失败，保留日志 " + tag + "，退出 " + str(result.returncode))
    return result


def save_progress(first=False):
    """原生文件fsync后，由固定WSL再fsync目录；不能默默跳过Windows目录落盘。"""
    data = (json.dumps(progress, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    checked(PROGRESS_FILE, "file", missing=first)
    temporary = PROGRESS_FILE if first else PROGRESS_FILE.with_name(PROGRESS_FILE.name + "." + uuid.uuid4().hex + ".tmp")
    write_new(temporary, data)
    if not first:
        checked(PROGRESS_FILE, "file")
        os.replace(temporary, PROGRESS_FILE)
    must(read(PROGRESS_FILE) == data, "完成记录替换后读回不等")
    helper = ("import os,sys,hashlib,pwd; "
              "assert pwd.getpwuid(os.geteuid()).pw_name=='qyq' and os.geteuid()!=0; "
              "p=sys.argv[1]; expected=sys.argv[2]; "
              "assert hashlib.sha256(open(p,'rb').read()).hexdigest()==expected; "
              "f=os.open(p,os.O_RDONLY|os.O_NOFOLLOW); os.fsync(f); os.close(f); "
              "d=os.open('.',os.O_RDONLY|os.O_DIRECTORY); os.fsync(d); os.close(d); "
              "d=os.open('fix_promotion_commands',os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW); os.fsync(d); os.close(d)")
    result = subprocess.run(wsl_command(REVIEW_DIR, ["/usr/bin/python3", "-c", helper, PROGRESS_FILE.name, sha(data)]),
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    must(result.returncode == 0, "WSL目录fsync失败，未继续：" + result.stderr.decode("utf-8", "replace"))
    must(read(PROGRESS_FILE) == data, "WSL目录fsync之后完成记录改变")


def parse_patch(data):
    lines = data.decode("utf-8").splitlines()
    operations, metadata = [], set()
    for number, line in enumerate(lines):
        must(not line.startswith(("GIT binary patch", "Binary files ", "rename from ", "rename to ", "copy from ", "copy to ", "old mode ", "new mode ")),
             "只接受已审文本片，不接二进制/改名/复制/模式改变")
        must(not (line.startswith(("index ", "new file mode ", "deleted file mode ")) and re.search(r"\b(?:120000|160000)\b", line)), "不接受链接或子模块")
        if line.startswith("diff --git "):
            fields = line.split(" ")
            must(len(fields) == 4 and fields[2].startswith("a/") and fields[3].startswith("b/"), "diff路径不明确")
            metadata.update(relative(value[2:]) for value in fields[2:])
        if not line.startswith("--- "):
            continue
        must(number + 1 < len(lines) and lines[number + 1].startswith("+++ "), "补丁头不完整")
        pair = []
        for value, prefix in ((line[4:], "a/"), (lines[number + 1][4:], "b/")):
            value = value.split("\t", 1)[0]
            if value == "/dev/null":
                pair.append(None)
            else:
                must(value.startswith(prefix), "补丁缺a/b前缀")
                name = relative(value[2:])
                must(name.startswith(PREFIX), "补丁目标越界")
                pair.append(name)
        before, after = pair
        must((before or after) and (before is None or after is None or before == after), "空目标或隐含改名")
        operations.append((before, after))
    targets = [a or b for a, b in operations]
    must(targets and len(targets) == len(set(targets)) and (not metadata or metadata == set(targets)), "补丁目标重复或元信息不同")
    return operations


def source_map(value, count):
    must(isinstance(value, dict) and len(value) == count, "完整源表数量不符")
    must(len({name.casefold() for name in value}) == len(value), "源表存在Windows大小写别名")
    for name, wanted in value.items():
        relative(name)
        must(not name.startswith(PREFIX) and sha_value(wanted), "源表必须为程序内相对路径及实际SHA")
    return {PREFIX + name: wanted for name, wanted in value.items()}


def triple(row):
    return {key: row[key] for key in ("index", "patch", "sha256")}


def validate_fix_evidence(value, old, review):
    """只读真实单修点结果；字段若不匹配就拒绝，不推导或补造退出0。"""
    must(value.get("format") == "sn100-integration-08-inventory-fix-v1" and value.get("complete") is True
         and value.get("full_674_replay_byte_equal") is True, "单修点来源回演未完成")
    must(value.get("source_before_sha256") == old["final_source_snapshot"]["sha256"]
         and value.get("source_after_sha256") == value.get("replay_source_sha256") == review["final_source_snapshot"]["sha256"], "单修点来源前后表不等")
    must(value.get("changed_files") == ["shell/build.sh"] and value.get("added_files") == []
         and value.get("deleted_files") == [] and value.get("baseline_files") == 674 and value.get("final_files") == 674,
         "单修点来源不是674→674且仅build.sh修改")
    must(value.get("ordinary_check_apply_actual_exits") == [0, 0]
         and value.get("patch") == {"path": review["patches"][5]["patch"], "sha256": review["patches"][5]["sha256"]}, "单修点补丁或普通退出记录不符")
    for key in ("compiled", "product_executed", "sql_executed", "network_used", "board_accessed", "formal_modified"):
        must(value.get(key) is False, "单修点来源范围不符：" + key)
    steps = value.get("steps")
    names = ["01_bash_n", "02_list_inputs", "03_git_init", "04_git_attributes", "05_plain_check", "06_plain_apply"]
    must(isinstance(steps, list) and [row.get("name") for row in steps] == names
         and all(row.get("actual_exit") == 0 for row in steps), "缺实际六步退出0记录")
    evidence_dir = PurePosixPath(review["fix_isolated_application"]["path"]).parent
    patch = ref_path(value["patch"]["path"])
    patch_wsl = "/mnt/" + patch.drive[0].lower() + "/" + patch.as_posix()[3:]
    for row in steps:
        must(row["command"][:5] == ["wsl.exe", "-d", "Ubuntu-24.04", "-u", "qyq"], "来源步骤不是固定WSL用户")
        for stream in ("stdout", "stderr"):
            reference({"path": (evidence_dir / (row["name"] + "." + stream + ".log")).as_posix(), "sha256": row[stream + "_sha256"]})
        must(read(ref_path((evidence_dir / (row["name"] + ".exit")).as_posix())).strip() == b"0", "来源原退出文件不是0")
    for row, args in ((steps[4], ["apply", "--check", patch_wsl]), (steps[5], ["apply", patch_wsl])):
        command = row["command"]
        must("/usr/bin/git" in command and command[command.index("/usr/bin/git"):] == GIT + args, "来源不是固定参数普通check/apply")
    inventory = value.get("input_inventory", {})
    must(inventory.get("seven_sources_and_four_headers_exactly_once") is True and inventory.get("compiled") is False
         and inventory.get("build_or_output_directories_created") is False, "来源没有确认七源四头各计一次")


def validate_contract(review):
    must(review.get("format") == "sn100-integration-08-final-merge-revision-02-v1"
         and review.get("review_approved") is True and review.get("formal_modified") is False
         and review.get("compiled") is False and review.get("product_executed") is False, "本次修订源码审查状态不符")
    for key, wanted in (("parent_promotion", PARENT_PROGRESS_SHA), ("parent_promotion_script", PARENT_SCRIPT_SHA), ("parent_final_review", PARENT_REVIEW_SHA)):
        must(review[key]["sha256"] == wanted, "不是固定的已完成五片父记录：" + key)
        reference(review[key])
    old = json_ref(review["parent_final_review"])
    parent = json_ref(review["parent_promotion"])
    must(old.get("format") == "sn100-integration-08-final-merge-v1" and old.get("review_approved") is True, "父审查格式不符")
    must(review["source07_snapshot"] == old["source07_snapshot"] and review["source07_archive"] == old["source07_archive"]
         and review["source07_snapshot"]["sha256"] == BASE_SHA and review["source07_archive"]["sha256"] == ARCHIVE_SHA
         and review["root_attributes"] == {"path": ".gitattributes", "sha256": ATTR_SHA}
         and review["git_apply_config"] == GIT_CONFIG, "07底稿、属性或固定Git参数不符")
    must(review["base_count"] == 663 and review["source_count"] == 674 and review["patch_count"] == 6, "修订链数量不符")
    base = source_map(review["base_source_files"], 663)
    final = source_map(review["final_source_files"], 674)
    must(review["base_source_files"] == old["base_source_files"] == json_ref(review["source07_snapshot"]), "官方07完整表不等")
    must(json_ref(review["final_source_snapshot"]) == review["final_source_files"], "修订最终快照不等")
    rows, checkpoints = review["patches"], review["checkpoints"]
    must(len(rows) == 6 and [x["index"] for x in rows] == list(range(1, 7))
         and rows[:5] == old["patches"] and len(checkpoints) == 7
         and [x["index"] for x in checkpoints] == list(range(7)) and checkpoints[:6] == old["checkpoints"], "旧五片或完整检查点对象被改写")
    wanted_applied = [triple(row) for row in rows[:5]]
    must(parent.get("format") == "sn100-integration-08-promotion-v1" and parent.get("complete") is True
         and parent.get("phase") == "complete" and parent.get("current_patch") is None
         and parent.get("applied") == wanted_applied and parent.get("preflight_applied") == list(range(1, 6))
         and parent.get("patches") == rows[:5] and parent.get("promotion_script_sha256") == PARENT_SCRIPT_SHA
         and parent.get("merge_review_sha256") == PARENT_REVIEW_SHA and parent.get("final_merge_review_sha256") == PARENT_REVIEW_SHA
         and parent.get("source_snapshot_sha256") == BASE_SHA and parent.get("git_apply_config") == GIT_CONFIG
         and parent.get("root_attributes") == review["root_attributes"] and parent.get("verified_source_paths") == 674
         and parent.get("base_source_files") == review["base_source_files"] and parent.get("final_source_files") == old["final_source_files"], "旧五片父完成记录不完整")
    for key in ("changed_files", "added_files", "deleted_files"):
        must(parent.get(key) == old.get(key), "父完成变更范围不等")
    names = set(base) | set(final)
    must(len(names) == 674, "完整联合源表不是674")
    previous = {name: base.get(name) for name in names}
    values = []
    for index, row in enumerate(rows):
        must(row["before_checkpoint"] == checkpoints[index] and row["after_checkpoint"] == checkpoints[index + 1], "行检查点未接完整链")
        before, after = json_ref(checkpoints[index]), json_ref(checkpoints[index + 1])
        must(set(before) == names and set(after) == names and before == previous, "全表检查点断链")
        for name in names:
            must((before[name] is None or sha_value(before[name])) and (after[name] is None or sha_value(after[name])), "检查点SHA格式不符")
        operations = parse_patch(reference({"path": row["patch"], "sha256": row["sha256"]}))
        targets = {a or b for a, b in operations}
        must(targets <= names and all(after[name] == before[name] for name in names - targets), "补丁检查点含额外变化")
        for a, b in operations:
            must((a is not None or before[b] is None) and (a is None or before[a] is not None)
                 and (b is not None or after[a] is None) and (b is None or after[b] is not None), "补丁新增/删除状态不符")
        if index == 5:
            must(operations == [(FIX_TARGET, FIX_TARGET)] and {name for name in names if before[name] != after[name]} == {FIX_TARGET}, "第6片只允许shell/build.sh普通文本修改")
        previous = after
        values.append(after)
    must(values[4] == {PREFIX + k: v for k, v in old["final_source_files"].items()} and previous == final, "父cp5或新cp6不等完整最终源表")
    for key, expected in (("changed_files", sorted(k for k in names if base.get(k) != final.get(k))),
                          ("added_files", sorted(k for k in names if k not in base and k in final)),
                          ("deleted_files", sorted(k for k in names if k in base and k not in final))):
        must(review.get(key) == expected, "合计六片变更范围不等：" + key)
    must(review["parent_isolated_application"] == old["single_stage_application"], "旧独立副本历史引用不符")
    historical = json_ref(review["parent_isolated_application"])
    must(historical.get("complete") is True and historical.get("five_standard_checks_and_applies_passed") is True
         and historical.get("independent_second_replay_executed") is False, "父五片来源回演历史不符")
    validate_fix_evidence(json_ref(review["fix_isolated_application"]), old, review)
    return old, parent, base, final, values


def check_inputs(review, old):
    must(sha(read(REVIEW_FILE)) == review_sha and sha(read(SELF)) == own_sha, "根审查或实际工具执行中改变")
    must(sha(read(ROOT / ".gitattributes")) == ATTR_SHA, "实际根属性改变")
    entries = [review[key] for key in ("source07_archive", "source07_snapshot", "root_attributes", "parent_promotion", "parent_promotion_script", "parent_final_review", "parent_isolated_application", "fix_isolated_application", "final_source_snapshot", "cross_patch_review")]
    if "historical_build_attempt" in review:
        entries.append(review["historical_build_attempt"])
    entries += review["fixed_inputs"] + old["fixed_inputs"] + review["checkpoints"]
    entries += [{"path": row["patch"], "sha256": row["sha256"]} for row in review["patches"]]
    for key in ("attribute_files", "historical_failed_checks"):
        entries += review.get(key, [])
    seen = {}
    for entry in entries:
        must(not entry["path"].casefold().startswith(PREFIX.casefold()), "正式旧CPP等可变源码不能充当immutable引用")
        must(entry["path"] not in seen or seen[entry["path"]] == entry["sha256"], "同一路径绑定多个SHA")
        seen[entry["path"]] = entry["sha256"]
        reference(entry)


def attributes(container, names):
    result = git(container, ["check-attr", "-z", "--all", "--stdin"], b"".join(name.encode("utf-8") + b"\0" for name in sorted(names)))
    return result.stdout


def check_git_root(container):
    must(git(container, ["rev-parse", "--show-toplevel"]).stdout.rstrip(b"\r\n") == wsl_path(container).encode(), "Git工作目录不是指定根")
    result = git(container, ["config", "--get", "core.eol"], allowed=(1,))
    must(result.stdout == b"", "core.eol不是原未设置状态；WSL启动器stderr按原样保留在日志")


def apply_one(container, row):
    data = reference({"path": row["patch"], "sha256": row["sha256"]})
    git(container, ["apply", "--check"], data)
    # check和apply都使用同一份已核原始bytes，不走命令行重解释或读别的补丁。
    must(reference({"path": row["patch"], "sha256": row["sha256"]}) == data, "check之后补丁改变")
    git(container, ["apply"], data)


def main():
    global progress, review_sha, own_sha
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--final-merge-review-sha256", required=True)
    args = parser.parse_args()
    must(os.name == "nt" and sys.platform == "win32", "仅用Windows原生Python做文件核验；Git仍固定WSL")
    must(REVIEW_DIR.name == "integration_08_revision_02_review" and ROOT / "temp/sn100_host/workflow_night_20261102" == TASK, "工具物理目录不符")
    checked(REVIEW_DIR, "dir")
    must(sha_value(args.final_merge_review_sha256), "根必须显式提供最终审查SHA")
    checked(PROGRESS_FILE, "file", missing=True)
    checked(LOG_DIR, "dir", missing=True)
    must(not PROGRESS_FILE.exists() and not LOG_DIR.exists(), "已有修订执行记录/日志；禁止自动重跑、恢复或回滚")
    review_sha = args.final_merge_review_sha256
    raw = read(REVIEW_FILE)
    must(sha(raw) == review_sha, "最终审查不是根显式指定SHA")
    review = json.loads(raw)
    own_sha = sha(read(SELF))
    make_dir(LOG_DIR)
    progress = {"format": "sn100-integration-08-promotion-revision-02-v1", "phase": "validating", "complete": False,
                "current_patch": None, "formal_write_may_have_started": False, "compiled": False, "program_executed": False,
                "final_merge_review_sha256": review_sha, "merge_review_sha256": review_sha, "promotion_script_sha256": own_sha,
                "scope": "patches/applied/preflight_applied为跨两次执行累计六片；旧五片继承父完成记录，本次只独立回演并正式应用第6片；本轮未编译，不抹去旧构建退出13历史。",
                "inherited_applied": [], "inherited_preflight": [], "applied_in_this_execution": [], "preflight_in_this_execution": []}
    try:
        save_progress(first=True)
        old, parent, base, final, checkpoints = validate_contract(review)
        check_inputs(review, old)
        before = checkpoints[4]
        stage = checked(ROOT / relative(review["stage_directory"]), "dir")
        stage.relative_to(TASK)
        must(stage != ROOT and stage != REVIEW_DIR and stage != TASK, "候选stage目录范围不符")
        attribute_stage = checked(ROOT / relative(review["attribute_stage_directory"]), "dir")
        attribute_stage.relative_to(TASK)
        must(attribute_stage != ROOT and attribute_stage != REVIEW_DIR and attribute_stage != TASK, "属性回演目录范围不符")
        verify_sources(ROOT, before)
        verify_sources(stage, final)
        compare_sources(attribute_stage, stage, final)
        must(read(stage / ".gitattributes") == read(attribute_stage / ".gitattributes") == read(ROOT / ".gitattributes"), "候选stage属性镜像不等")
        progress.update({"phase": "preflight", "source_snapshot_sha256": BASE_SHA,
                         "base_source_files": review["base_source_files"], "final_source_files": review["final_source_files"],
                         "patches": review["patches"], "applied": list(parent["applied"]), "preflight_applied": list(parent["preflight_applied"]),
                         "inherited_applied": list(parent["applied"]), "inherited_preflight": list(parent["preflight_applied"]),
                         "parent_record": review["parent_promotion"], "parent_script": review["parent_promotion_script"],
                         "parent_review": review["parent_final_review"], "git_apply_config": GIT_CONFIG, "root_attributes": review["root_attributes"]})
        save_progress()
        preflight = REVIEW_DIR / ("fix_promotion_preflight_" + uuid.uuid4().hex)
        make_dir(preflight)
        progress["preflight"] = preflight.relative_to(ROOT).as_posix()
        save_progress()
        for name in sorted(before):
            path = preflight / name
            checked(path, "file", missing=True)
            path.parent.mkdir(parents=True, exist_ok=True)
            checked(path.parent, "dir")
            write_new(path, read(source_path(ROOT, name)))
        write_new(preflight / ".gitattributes", read(ROOT / ".gitattributes"))
        git(preflight, ["init", "-q"])
        for container in (ROOT, attribute_stage, preflight):
            check_git_root(container)
        names = set(final)
        def check_attributes():
            current = attributes(ROOT, names)
            must(current == attributes(attribute_stage, names) == attributes(preflight, names), "正式/原单片回演/新回演完整--all属性作用不等")
            must(read(stage / ".gitattributes") == read(attribute_stage / ".gitattributes") == read(ROOT / ".gitattributes"), "候选属性镜像执行中改变")
        check_attributes()
        row = review["patches"][5]
        check_inputs(review, old)
        verify_sources(preflight, before)
        apply_one(preflight, row)
        verify_sources(preflight, final)
        compare_sources(preflight, stage, final)
        verify_sources(ROOT, before)
        check_inputs(review, old)
        check_attributes()
        progress["preflight_applied"].append(6)
        progress["preflight_in_this_execution"].append(6)
        progress["phase"] = "applying"
        save_progress()
        verify_sources(ROOT, before)
        compare_sources(preflight, stage, final)
        check_inputs(review, old)
        check_attributes()
        progress["current_patch"] = triple(row)
        progress["formal_write_may_have_started"] = True
        save_progress()  # 正式Git check/apply前，先把本次第6片意图及目录真实落盘。
        apply_one(ROOT, row)  # 唯一正式应用调用；没有正式应用前五片的循环。
        verify_sources(ROOT, final)
        compare_sources(ROOT, stage, final)
        compare_sources(ROOT, preflight, final)
        check_inputs(review, old)
        check_attributes()
        progress["applied"].append(triple(row))
        progress["applied_in_this_execution"].append(triple(row))
        progress.update({"current_patch": None, "phase": "complete", "complete": True, "verified_source_paths": len(final),
                         "changed_files": review["changed_files"], "added_files": review["added_files"], "deleted_files": review["deleted_files"]})
        save_progress()
        print("INHERITED_FORMAL_PATCHES=5\nFORMAL_PATCHES_APPLIED_THIS_EXECUTION=1\nCUMULATIVE_FORMAL_PATCHES=6\nFORMAL_PATHS_MATCH_REVIEW=674\nPROMOTION_COMPLETE=1\nPRODUCT_EXECUTED=0")
    except BaseException as error:
        progress.update({"phase": "failed_requires_review", "complete": False, "error": repr(error)})
        try:
            save_progress(first=not PROGRESS_FILE.exists())
        except BaseException as record_error:
            print("FAILED_TO_DURABLY_RECORD_FAILURE=" + repr(record_error), file=sys.stderr)
        raise


if __name__ == "__main__":
    main()
