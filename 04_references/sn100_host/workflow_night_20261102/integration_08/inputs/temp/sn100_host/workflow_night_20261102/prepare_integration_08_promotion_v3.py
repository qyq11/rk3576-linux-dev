"""根显式给最终五片清单SHA后生成08晋级脚本；本工具不应用补丁或构建。"""
from pathlib import Path, PurePosixPath
import argparse
import ast
import hashlib
import json
import os
import re
import stat

BASE_SHA256 = "fd5614ad0673ab6896eeea1f7502ac09ec8a4ac4f98a338f5967a4ad3717fdaf"
ARCHIVE_SHA256 = "18eaa7fb091d82cddee883c52ac8d6d50d2d86d9d65cd6f25cff99cd74520b41"
ATTRIBUTES_SHA256 = "ac7fd8f4c639db19a3f6192a1cf4205bcc4ef06a573a29a2a98653fd19ae3bf8"
TEMPLATE_SHA256 = "4d9ec71bf5b0aac30f9db2e3c5e543ec05c38aac9e3be121bc10bd92021476ee"
PREFIX = "06_programs/sn100_host/"
MARKER = "__ROOT_FINAL_REVIEW_SHA256__"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def regular(path):
    if not stat.S_ISREG(path.lstat().st_mode):
        raise RuntimeError("不是普通输入文件：" + str(path))
    return path.read_bytes()


def relative_path(root, name):
    if (not isinstance(name, str) or not name or "\\" in name or ":" in name or '"' in name
            or any(ord(char) < 32 for char in name) or any(part in {"", ".", ".."} for part in name.split("/"))
            or PurePosixPath(name).is_absolute()):
        raise RuntimeError("输入路径不是可核相对路径：" + str(name))
    path = root / name
    path.resolve().relative_to(root.resolve())
    for entry in (path, *path.parents):
        if entry.is_symlink() or (hasattr(entry, "is_junction") and entry.is_junction()):
            raise RuntimeError("输入路径经过链接：" + str(entry))
        if entry == root:
            break
    return path


def reference(root, entry):
    if not re.fullmatch(r"[a-f0-9]{64}", entry["sha256"]):
        raise RuntimeError("引用SHA格式不符。")
    data = regular(relative_path(root, entry["path"]))
    if sha(data) != entry["sha256"]:
        raise RuntimeError("固定输入已变化：" + entry["path"])
    return data


def source_files(root, container, mapping):
    for name, expected in mapping.items():
        if not name.startswith(PREFIX):
            raise RuntimeError("源表目标越出sn100_host。")
        path = relative_path(container, name)
        if expected is None:
            if path.exists() or path.is_symlink():
                raise RuntimeError("应新增的目标已有文件，未覆盖：" + name)
        elif not re.fullmatch(r"[a-f0-9]{64}", expected) or sha(regular(path)) != expected:
            raise RuntimeError("源文件与固定快照不同：" + str(path.relative_to(root)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--final-merge-review-sha256", required=True)
    args = parser.parse_args()
    wanted = args.final_merge_review_sha256
    if not re.fullmatch(r"[a-f0-9]{64}", wanted):
        raise RuntimeError("必须显式传根已确认的最终清单64位SHA-256。")
    task = Path(__file__).resolve().parent
    root = task.parents[2]
    output = task / "promote_integration_08_review.py"
    if output.exists() or output.is_symlink():
        raise RuntimeError("08固化晋级脚本已存在，拒绝覆盖或重复生成。")
    review_path = task / "integration_08_review/final_merge_review.json"
    raw = regular(review_path)
    if sha(raw) != wanted:
        raise RuntimeError("当前最终清单不是根显式给定的版本。")
    review = json.loads(raw)
    if (review.get("format") != "sn100-integration-08-final-merge-v1" or review.get("patch_count") != 5
            or review.get("base_count") != 663 or review.get("source_count") != len(review.get("final_source_files", {}))
            or any(review.get(key) is not False for key in ("formal_modified", "compiled", "product_executed", "database_opened", "network_used"))):
        raise RuntimeError("必须是已审五片、官方663项起点和动态最终源数的冻结源码清单。")
    if (review["source07_snapshot"]["sha256"] != BASE_SHA256 or review["source07_archive"]["sha256"] != ARCHIVE_SHA256
            or review["root_attributes"] != {"path": ".gitattributes", "sha256": ATTRIBUTES_SHA256}):
        raise RuntimeError("不是官方07源码包、快照和根路径属性。")
    base, final = review["base_source_files"], review["final_source_files"]
    if (len(base) != 663 or len(final) != review["source_count"] or json.loads(reference(root, review["source07_snapshot"])) != base
            or json.loads(reference(root, review["final_source_snapshot"])) != final):
        raise RuntimeError("底稿、最终源码及独立回演全源表不符。")
    cfg = review["git_apply_config"]
    if cfg != {"implementation": "/usr/bin/git", "wsl_distribution": "Ubuntu-24.04", "user": "qyq",
               "flags": ["-c", "core.autocrlf=false", "-c", "core.safecrlf=false", "-c", "core.whitespace=cr-at-eol"],
               "core_eol": "unset"}:
        raise RuntimeError("不是实际回演的WSL Git和行尾参数。")
    for entry in (review["source07_archive"], review["root_attributes"], review["cross_patch_review"],
                  *review["fixed_inputs"], *review["attribute_files"], *review["historical_failed_checks"]):
        reference(root, entry)
    replay = json.loads(reference(root, review["single_stage_application"]))
    if (replay.get("format") != "sn100-integration-08-single-isolated-application-v1"
            or replay.get("complete") is not True or replay.get("five_standard_checks_and_applies_passed") is not True
            or replay.get("root674_map_and_default_seven_after_bytes_equal") is not True
            or replay.get("independent_second_replay_executed") is not False):
        raise RuntimeError("五片普通回演没有完整通过。")
    names = {PREFIX + name for name in base} | {PREFIX + name for name in final}
    initial = {name: base.get(name[len(PREFIX):]) for name in names}
    expected = {name: final.get(name[len(PREFIX):]) for name in names}
    rows = review["patches"]
    checkpoints = review["checkpoints"]
    if len(rows) != 5 or [row["index"] for row in rows] != list(range(1, 6)) or [row["index"] for row in checkpoints] != list(range(6)):
        raise RuntimeError("五片顺序或六个完整检查点不齐。")
    previous = initial
    for index, row in enumerate(rows):
        reference(root, {"path": row["patch"], "sha256": row["sha256"]})
        before = json.loads(reference(root, row["before_checkpoint"]))
        after = json.loads(reference(root, row["after_checkpoint"]))
        if (set(before) != names or set(after) != names or before != previous
                or json.loads(reference(root, checkpoints[index])) != before
                or json.loads(reference(root, checkpoints[index + 1])) != after):
            raise RuntimeError("完整源路径检查点不连续。")
        previous = after
    if previous != expected:
        raise RuntimeError("最后检查点不是最终全源。")
    changed = sorted(name for name in names if initial[name] != expected[name])
    added = sorted(name for name in names if initial[name] is None and expected[name] is not None)
    deleted = sorted(name for name in names if initial[name] is not None and expected[name] is None)
    if (changed != review["changed_files"] or added != review["added_files"] or deleted != review["deleted_files"]
            or deleted):
        raise RuntimeError("变化目标与已审动态清单不等，或含未获准删除。")
    source_files(root, root, initial)
    for key in ("stage_directory",):
        container = relative_path(root, review[key])
        container.resolve().relative_to(task.resolve())
        source_files(root, container, expected)
        if regular(container / ".gitattributes") != reference(root, review["root_attributes"]):
            raise RuntimeError("隔离副本未逐字镜像根路径属性。")
    template_path = task / "promote_integration_08_template_v3.py.in"
    template = regular(template_path)
    if sha(template) != TEMPLATE_SHA256:
        raise RuntimeError("已核08模板已变化，未生成。")
    source = template.decode("utf-8")
    if source.count(MARKER) != 1:
        raise RuntimeError("模板固定SHA占位不唯一。")
    source = source.replace(MARKER, wanted)
    ast.parse(source)
    if sha(regular(review_path)) != wanted or sha(regular(template_path)) != TEMPLATE_SHA256:
        raise RuntimeError("生成期间清单或模板发生变化。")
    with output.open("xb") as stream:
        stream.write(source.encode("utf-8"))
        stream.flush()
        os.fsync(stream.fileno())
    if os.name != "nt":
        descriptor = os.open(output.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    print("PROMOTION_08_SCRIPT_GENERATED=1\nFORMAL_SOURCE_MODIFIED=0\nBUILD_EXECUTED=0")
    print("PROMOTER_SHA256=" + sha(regular(output)))


if __name__ == "__main__":
    main()
