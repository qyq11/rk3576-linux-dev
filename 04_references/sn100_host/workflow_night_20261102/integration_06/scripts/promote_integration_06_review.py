"""经授权后晋级第六轮八片；本脚本不构建、不运行产品、不联网。

先核第五轮639项底稿和八片指纹，在temp预演普通git apply并逐字节核隔离副本，
再顺序应用正式窄补丁。任何中断保留progress，拒绝自动重跑或逆向回滚。
"""
from pathlib import Path, PurePosixPath
import hashlib
import json
import os
import re
import stat
import subprocess
import uuid

PREFIX = "06_programs/sn100_host/"
BASE_SHA256 = "e785702f907aecee9bc645614f122c05f1c6053d33ecb1b94f7e08ee8eaaf111"
REVIEW_SHA256 = "e48ec9c5d5c3ffa9ee63f248dad9482536aebc7a48b871c6e00b1941513a13fa"
PATCHES = (('report_lis/channel_switch_candidate_v2/channel_switch.patch',
  'bdae1b3d7a0220ac77ff588e515988b2e9687f5a1689864653c95edf0445465d'),
 ('project_update_intents/project_update_intents.patch',
  '250b88f58b0e10b365aa3ae4a0c0005eef2e3619676e6fe659b607aa7822963c'),
 ('report_lis/qq_actual_count_review/qq_count_tail.patch',
  '9d53133db944e6ce430c50cd926933ef16ef263d1f3e4291efb0117984cc64a9'),
 ('project_copy_v2/copy_core_and_ui.patch',
  'a4ab1ade63b6bb558bb856874f36718ffb1ef65eaad7ad11af1b8de322656ef8'),
 ('project_copy_v2/wiring.patch', '9e57b4817616ae7d7220dcf47c6c4e419312787c195ab12465e540a8ce011e76'),
 ('project_copy_v2/external_writer_guards.patch',
  '6c49a885f73671b5f732c38a734121dab011b27ed229c8b658f598ea23e7001b'),
 ('integration_06_bridges/copy_cal_guard_after_intents.patch',
  'eb048b9d6bd69b445256cf7e3a012aecffe5251e35980ddd0ab20c0d5b8cc351'),
 ('project_update_intents_v2/delta_from_250b.patch',
  '68b5f47ece7f544a8200a792ec15ee8baaff2876a9de93911f9ce3139eb9bfd9'))

task = Path(__file__).resolve().parent
root = task.parents[2]
stage = task / "integration_06_review"
progress_file = stage / "promotion_progress.json"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_relative(name):
    if (not isinstance(name, str) or not name or "\\" in name or ":" in name or '"' in name
            or any(ord(char) < 32 for char in name) or any(part in {"", ".", ".."} for part in name.split("/"))
            or PurePosixPath(name).is_absolute()):
        raise RuntimeError("路径不是可核对的项目相对路径：" + str(name))
    return name


def target_path(container, name):
    safe_relative(name)
    if not name.startswith(PREFIX) or name == PREFIX.rstrip("/"):
        raise RuntimeError("补丁或清单路径越出sn100_host：" + name)
    path = container / name
    path.resolve().relative_to((container / PREFIX.rstrip("/")).resolve())
    for entry in (path, *path.parents):
        if entry.is_symlink() or (hasattr(entry, "is_junction") and entry.is_junction()):
            raise RuntimeError("目标路径经过链接，未继续：" + str(entry))
        if entry == container:
            break
    return path


def read_regular(path):
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode):
        raise RuntimeError("需要普通文件：" + str(path))
    return path.read_bytes()


def verify_files(container, expected):
    for name, wanted in expected.items():
        path = target_path(container, name)
        if wanted is None:
            if path.exists() or path.is_symlink():
                raise RuntimeError("应不存在的目标已有文件，未覆盖：" + name)
        elif digest(read_regular(path)) != wanted:
            raise RuntimeError("输入或应用结果发生变化：" + name)


def compare_review(container, expected):
    verify_files(container, expected)
    verify_files(stage, expected)
    for name, wanted in expected.items():
        if wanted is not None and read_regular(target_path(container, name)) != read_regular(target_path(stage, name)):
            raise RuntimeError("与隔离副本逐字节不同：" + name)


def parse_patch(data):
    lines = data.decode("utf-8").splitlines()
    operations, metadata = [], set()
    for index, line in enumerate(lines):
        if line.startswith(("GIT binary patch", "Binary files ", "rename from ", "rename to ", "copy from ", "copy to ", "old mode ", "new mode ")):
            raise RuntimeError("本工具只接受已审文本窄片，不处理二进制、改名、复制或模式变更。")
        if re.search(r"\b(?:120000|160000)\b", line) and line.startswith(("index ", "new file mode ", "deleted file mode ")):
            raise RuntimeError("不允许链接或子模块目标。")
        if line.startswith("diff --git "):
            fields = line.split(" ")
            if len(fields) != 4 or not fields[2].startswith("a/") or not fields[3].startswith("b/"):
                raise RuntimeError("无法明确识别diff路径。")
            for field in fields[2:]:
                name = field[2:]
                target_path(root, name)
                metadata.add(name)
        if not line.startswith("--- "):
            continue
        if index + 1 >= len(lines) or not lines[index + 1].startswith("+++ "):
            raise RuntimeError("补丁文件头不完整。")
        names = []
        for value, prefix in ((line[4:], "a/"), (lines[index + 1][4:], "b/")):
            value = value.split("\t", 1)[0]
            if value == "/dev/null":
                names.append(None)
            else:
                if not value.startswith(prefix):
                    raise RuntimeError("补丁文件头缺少明确a/b前缀。")
                name = value[2:]
                target_path(root, name)
                names.append(name)
        before, after = names
        if (before is None and after is None) or (before is not None and after is not None and before != after):
            raise RuntimeError("不接受空目标或隐含改名。")
        operations.append((before, after))
    targets = [before or after for before, after in operations]
    if not targets or len(targets) != len(set(targets)) or (metadata and metadata != set(targets)):
        raise RuntimeError("补丁目标为空、重复或与diff元信息不符。")
    return operations


def sync_parent(path):
    if os.name != "nt":
        descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def save_progress(value, first=False):
    data = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    temporary = progress_file if first else progress_file.with_name(progress_file.name + "." + uuid.uuid4().hex + ".tmp")
    with temporary.open("xb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())
    if not first:
        os.replace(temporary, progress_file)
    sync_parent(progress_file)


def apply_patch(patch, wanted, directory=None):
    data = read_regular(patch)
    if digest(data) != wanted:
        raise RuntimeError("实际应用补丁已变化：" + str(patch))
    command = ["git", "apply", "--whitespace=error-all"]
    if directory is not None:
        command.append("--directory=" + directory.relative_to(root).as_posix())
    subprocess.run(command + ["--check"], input=data, cwd=root, check=True)
    # 不加ignore-space-change、3way或reject；不复制整文件覆盖正式源码。
    subprocess.run(command, input=data, cwd=root, check=True)


def main():
    if progress_file.exists() or progress_file.is_symlink():
        raise RuntimeError("已有晋级记录，请先人工核对，不自动重复应用。")
    stage.resolve().relative_to(task.resolve())
    if stage.is_symlink() or not stage.is_dir():
        raise RuntimeError("隔离副本目录不可用。")
    base_path = task / "integration_05/source_snapshot.json"
    base_bytes = read_regular(base_path)
    if digest(base_bytes) != BASE_SHA256:
        raise RuntimeError("第五轮固定底稿清单已变化。")
    base = json.loads(base_bytes)
    review_bytes = read_regular(stage / "merge_review.json")
    if digest(review_bytes) != REVIEW_SHA256:
        raise RuntimeError("06隔离副本清单已变化。")
    review = json.loads(review_bytes)
    ordered = tuple((item["patch"], item["sha256"]) for item in review["applied_patches"])
    if ordered != PATCHES or review["source_base"] != "integration_05/source_snapshot.json" or len(base) != review["base_files"]:
        raise RuntimeError("隔离副本不是本次已审八片或底稿。")
    expected = {}
    for name, value in base.items():
        safe_relative(name)
        if not re.fullmatch(r"[a-f0-9]{64}", value):
            raise RuntimeError("底稿指纹格式无效。")
        expected[PREFIX + name] = value
    verify_files(root, expected)
    patches, present, targets = [], set(expected), set()
    for name, wanted in PATCHES:
        safe_relative(name)
        path = task / name
        path.resolve().relative_to(task.resolve())
        data = read_regular(path)
        if digest(data) != wanted:
            raise RuntimeError("固定补丁已变化：" + name)
        operations = parse_patch(data)
        for before, after in operations:
            if before is not None and before not in present:
                raise RuntimeError("修改/删除目标没有受核底稿：" + before)
            if before is None and after in present:
                raise RuntimeError("新增目标与已受核文件冲突：" + after)
            if before is not None:
                present.remove(before)
            if after is not None:
                present.add(after)
            targets.add(before or after)
        patches.append((path, wanted, operations))
    for name in targets - set(expected):
        expected[name] = None
    verify_files(root, expected)  # 新增目标必须不存在，不能覆盖已有无关未提交文件。
    for name in targets:
        path = target_path(root, name)
        if path.exists() and path.lstat().st_nlink != 1:
            raise RuntimeError("补丁目标存在其他硬链接，未写入：" + name)
    final = {name: digest(read_regular(target_path(stage, name))) if name in present else None for name in expected}
    verify_files(stage, final)
    progress = {"format": "sn100-integration-06-promotion-v1", "phase": "preflight", "complete": False,
                "source_snapshot_sha256": BASE_SHA256, "merge_review_sha256": digest(review_bytes),
                "patches": [{"patch": name, "sha256": value} for name, value in PATCHES],
                "preflight_applied": [], "applied": [], "current_patch": None,
                "formal_write_may_have_started": False, "compiled": False, "program_executed": False}
    save_progress(progress, first=True)
    try:
        # 保留纯temp预演树，便于中断后核查；不递归清理任何已有目录。
        preflight = stage / ("promotion_preflight_" + uuid.uuid4().hex)
        preflight.mkdir(exist_ok=False)
        progress["preflight"] = preflight.relative_to(root).as_posix()
        save_progress(progress)
        for name, value in expected.items():
            if value is not None:
                destination = target_path(preflight, name)
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(read_regular(target_path(root, name)))
        checkpoints, current = [], dict(expected)
        for index, (patch, wanted, operations) in enumerate(patches):
            if digest(read_regular(patch)) != wanted:
                raise RuntimeError("预演前补丁已变化。")
            apply_patch(patch, wanted, preflight)
            for before, after in operations:
                name = before or after
                current[name] = digest(read_regular(target_path(preflight, name))) if after is not None else None
            verify_files(preflight, current)
            checkpoints.append(dict(current))
            progress["preflight_applied"].append(index + 1)
            save_progress(progress)
        compare_review(preflight, final)
        verify_files(root, expected)
        for patch, wanted, _ in patches:
            if digest(read_regular(patch)) != wanted:
                raise RuntimeError("正式应用前补丁已变化。")
        progress["phase"] = "applying"
        for index, (patch, wanted, _) in enumerate(patches):
            verify_files(root, expected if index == 0 else checkpoints[index - 1])
            if digest(read_regular(patch)) != wanted:
                raise RuntimeError("当前补丁已变化。")
            progress["current_patch"] = {"index": index + 1, "patch": PATCHES[index][0], "sha256": wanted}
            progress["formal_write_may_have_started"] = True
            save_progress(progress)  # 先记在途，避免已应用但来不及写记录。
            apply_patch(patch, wanted)
            verify_files(root, checkpoints[index])
            progress["applied"].append(progress["current_patch"])
            progress["current_patch"] = None
            save_progress(progress)
        compare_review(root, final)  # 覆盖base及所有新增/删除目标；不容忍仅换行差异。
        for patch, wanted, _ in patches:
            if digest(read_regular(patch)) != wanted:
                raise RuntimeError("收尾时固定补丁已变化。")
        progress.update({"phase": "complete", "complete": True, "verified_source_paths": len(final),
                         "changed_files": [name for name, value in final.items() if value != expected[name]],
                         "added_files": [name for name, value in final.items() if expected[name] is None and value is not None],
                         "deleted_files": [name for name, value in final.items() if expected[name] is not None and value is None],
                         "final_source_files": {name[len(PREFIX):]: value for name, value in final.items() if value is not None},
                         "promotion_script_sha256": digest(read_regular(Path(__file__)))})
        save_progress(progress)
        print("FORMAL_PATCHES_APPLIED=8\nFORMAL_PATHS_MATCH_REVIEW=" + str(len(final)) + "\nPRODUCT_EXECUTED=0")
    except BaseException as error:
        progress["phase"] = "failed_requires_review"
        progress["error"] = str(error)
        save_progress(progress)
        raise


if __name__ == "__main__":
    main()
