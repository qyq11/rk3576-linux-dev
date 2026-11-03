"""经授权后晋级第五轮四片；本脚本不构建、不运行产品、不联网。

先核第四轮全部底稿和四片指纹，在temp预演普通git apply并逐字节核隔离副本，
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
BASE_SHA256 = "209a9fd27636d611ca691e6fc0c18059351d15dbd232e7729bf90089d3e1c5cd"
PATCHES = (
    ("report_average_config/report_average_config.patch", "b58f03caaea1da2333607a35ed8de2c9ebf8fb761f87d6ea5afcd2ef40f5642b"),
    ("theme_consumers/theme_consumers.patch", "be48e2e9531f1e9adbeec6fa6b5eec44c30eb9046e4fb294efce9c8109a69fcd"),
    ("device_reconnect_v2/device_reconnect_v2.patch", "99dd8eda52f99e7e533e4fe5b3b0a95004c01dc703a68474bee16e228fef97c6"),
    ("reagent_group_enable_v2/reagent_group_enable_v2.patch", "775d4cf0bdd679af087ab40210852d3a5971b26bbe7c9dfa668877a9e0d5569a"),
)
task = Path(__file__).resolve().parent
root = task.parents[2]
stage = task / "integration_05_review"
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
    base_path = task / "integration_04/source_snapshot.json"
    base_bytes = read_regular(base_path)
    if digest(base_bytes) != BASE_SHA256:
        raise RuntimeError("第四轮固定底稿清单已变化。")
    base = json.loads(base_bytes)
    review_bytes = read_regular(stage / "merge_review.json")
    review = json.loads(review_bytes)
    ordered = tuple((item["patch"], item["sha256"]) for item in review["applied_patches"])
    if ordered != PATCHES or review["source_base"] != "integration_04/source_snapshot.json" or len(base) != review["base_files"]:
        raise RuntimeError("隔离副本不是本次已审四片或底稿。")
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
    progress = {"format": "sn100-integration-05-promotion-v1", "phase": "preflight", "complete": False,
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
                         "deleted_files": [name for name, value in final.items() if expected[name] is not None and value is None]})
        save_progress(progress)
        print("FORMAL_PATCHES_APPLIED=4\nFORMAL_PATHS_MATCH_REVIEW=" + str(len(final)) + "\nPRODUCT_EXECUTED=0")
    except BaseException as error:
        progress["phase"] = "failed_requires_review"
        progress["error"] = str(error)
        save_progress(progress)
        raise


if __name__ == "__main__":
    main()
