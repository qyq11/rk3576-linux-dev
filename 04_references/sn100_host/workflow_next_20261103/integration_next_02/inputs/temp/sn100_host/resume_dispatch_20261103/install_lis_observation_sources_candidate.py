#!/usr/bin/env python3
"""供总控审阅后显式安装观察源码；本文件交付时尚未执行。

只有 --apply 才允许创建证据、应用补丁和复制两份独立工具。没有编译、
产品启动、板卡、网络、清理、提交或回退动作。失败保留现场，不覆盖重试。
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "06_programs/sn100_host"
ADOPTIONS = ROOT / "temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01"
INITIAL = ADOPTIONS / "lis_profile_queue/source_after.json"
INITIAL_SHA = "c8bf26a4247c03060feb778d250bf9b5c14a9b93f3bcd550081fb617c88c8c18"
OUT = ADOPTIONS / "observer_04"
P04 = ROOT / "temp/sn100_host/board_b/20261103_lis_keep_running_candidate_04"
TOOL_RELATIVE = "05_tools/tests/sn100_host/lis_keep_running_20261103"
TOOL_DIRECTORY = ROOT / TOOL_RELATIVE
CPP = "shell/lis_keep_running_observation.cpp"
HEADER = "shell/lis_connection_observer_09.h"
PRI = "shell/shell.pri"
BUILD = "shell/build.sh"
SOURCE_MAPPING = {
    "lis_keep_running_review.cpp": CPP,
    "lis_connection_observer_09.h": HEADER,
}
TOOL_NAMES = ("wait_lis_observation.sh", "review_lis_observation.py")
P04_HASHES = {
    "lis_connection_observer_09.h": "44bae228d4b189278b9d3eb93afa5698ea91108a489db0143ca2b1fa5b668ada",
    "lis_keep_running_review.cpp": "ddeb47ff1b3175c0366c0671122117e2cdd585089f1e933a0846c5b0bc8cf543",
    "review_lis_observation.py": "a0ea76de798d3fee19e5bb0adf547d033d3619f5020e05a5f61ce6cbc1c46783",
    "wait_lis_observation.sh": "eff1fddabbb5d8bf89d54f3d2b45a78760e8d8a9738fd71047baa7730aad1de8",
}
REVIEWS = {
    "lis_keep_running_independent_review.md": "ecc83dd08ccea8b6feaadb86f90403ae0fc1868fc70590341ccd95f53d760e19",
    "lis_keep_running_03_targeted_review.md": "13b7bfc44bf29313d72def22068e6942f005b64714cda02efea87580502718bb",
    "lis_keep_running_04_targeted_review.md": "215b06d96911ecd063cdbbdec7f2ac1332a08135b2ee3ff69533d7778cb1ed80",
}
HASH = re.compile(r"[0-9a-f]{64}\Z")


class InstallationError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise InstallationError(message)


def fingerprint(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def regular_bytes(path: Path) -> bytes:
    require(path.is_file() and not path.is_symlink(), f"不是实际普通文件：{path}")
    return path.read_bytes()


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        require(key not in result, "JSON存在重复键。")
        result[key] = value
    return result


def decode_map(raw: bytes, count: int) -> dict[str, str]:
    value = json.loads(raw.decode("utf-8-sig"), object_pairs_hook=unique_object)
    require(type(value) is dict and len(value) == count, f"冻结清单必须恰有{count}项。")
    for name, digest in value.items():
        require(type(name) is str and type(digest) is str and bool(HASH.fullmatch(digest)), "清单路径或SHA不合法。")
        path = PurePosixPath(name)
        require(not path.is_absolute() and str(path) == name and ".." not in path.parts
                and "\\" not in name and bool(path.parts), "清单含非规范相对路径。")
    return value


def actual_map(expected: dict[str, str]) -> dict[str, str]:
    # 全表的文件集合沿用总控冻结定义，不重新扫描参考工程或运行资料。
    return {name: fingerprint(regular_bytes(PROGRAM / name)) for name in expected}


def require_absent(path: Path) -> None:
    require(not path.exists() and not path.is_symlink(), f"路径已存在，拒绝覆盖：{path}")


def write_new(path: Path, raw: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(raw)


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def save(name: str, value: object) -> None:
    write_new(OUT / name, json_bytes(value))


def insert_after_once(raw: bytes, anchor: bytes, addition: bytes) -> bytes:
    require(raw.count(anchor) == 1, "构建登记锚点不唯一或已变更。")
    require(addition not in raw, "构建登记已经存在。")
    return raw.replace(anchor, anchor + addition, 1)


def candidate_inputs() -> tuple[dict[str, bytes], bytes]:
    manifest_raw = regular_bytes(P04 / "after_manifest.json")
    require(decode_map(manifest_raw, 4) == P04_HASHES, "P04四源清单与已审指纹不符。")
    images = {}
    for name, digest in P04_HASHES.items():
        raw = regular_bytes(P04 / "candidate" / name)
        require(fingerprint(raw) == digest, f"P04已审源码改变：{name}")
        images[name] = raw
    return images, manifest_raw


def build_postimages(before: dict[str, bytes], candidate: dict[str, bytes]) -> dict[str, bytes]:
    # 保留旧字节及混合行尾；只在精确锚点后增加登记，不解码重写整份文件。
    for raw in before.values():
        require(b"lis_keep_running_observation.cpp" not in raw
                and b"lis_connection_observer_09.h" not in raw,
                "正式构建已经存在本次观察器登记。")
    pri_anchor = b"SOURCES += $$PWD/lis_shell.cpp\r\n"
    pri_addition = (b"SOURCES += $$PWD/lis_keep_running_observation.cpp\r\n"
                    b"HEADERS += $$PWD/lis_connection_observer_09.h\r\n")
    pri = insert_after_once(before[PRI], pri_anchor, pri_addition)
    build_source_anchor = (b'sources+=("$program_dir/lis_transport/lis_mllp_codec.cpp" '
                           b'"$program_dir/lis_transport/lis_tcp_transport.cpp" '
                           b'"$program_dir/lis_transport/lis_connection_controller.cpp")\n')
    build_header_anchor = (b'headers+=("$program_dir/lis_transport/lis_tcp_transport.h" '
                           b'"$program_dir/lis_transport/lis_mllp_codec.h" '
                           b'"$program_dir/lis_transport/lis_connection_controller.h" '
                           b'"$program_dir/lis_transport/lis_connection_values.h")\n')
    build = insert_after_once(before[BUILD], build_source_anchor,
                              b'sources+=("$module_dir/lis_keep_running_observation.cpp")\n')
    build = insert_after_once(build, build_header_anchor,
                              b'headers+=("$module_dir/lis_connection_observer_09.h")\n')
    return {CPP: candidate["lis_keep_running_review.cpp"],
            HEADER: candidate["lis_connection_observer_09.h"], PRI: pri, BUILD: build}


def make_patch(before: dict[str, bytes], after: dict[str, bytes]) -> bytes:
    pieces = []
    for name in sorted(after):
        repository_path = "06_programs/sn100_host/" + name
        left = ("a/" + repository_path).encode("ascii")
        right = ("b/" + repository_path).encode("ascii")
        pieces.append(b"diff --git " + left + b" " + right + b"\n")
        if name not in before:
            pieces.append(b"new file mode 100644\n")
        pieces.extend(difflib.diff_bytes(
            difflib.unified_diff, before.get(name, b"").splitlines(keepends=True),
            after[name].splitlines(keepends=True),
            fromfile=left if name in before else b"/dev/null", tofile=right, lineterm=b"\n"))
    patch = b"".join(pieces)
    require(bool(patch) and patch.count(b"diff --git ") == 4, "补丁范围不是四个正式文件。")
    return patch


def run_git(phase: str, patch: Path, commands: list[dict]) -> int:
    argv = ["git", "-c", "core.autocrlf=false", "apply"]
    if phase == "check":
        argv.append("--check")
    argv.append(str(patch))
    result = subprocess.run(argv, cwd=ROOT, capture_output=True, check=False)
    write_new(OUT / f"{phase}.stdout.log", result.stdout)
    write_new(OUT / f"{phase}.stderr.log", result.stderr)
    record = {"phase": phase, "argv": argv, "cwd": str(ROOT), "actual_exit": result.returncode}
    commands.append(record)
    save(f"{phase}_result.json", record)
    return result.returncode


def install(run_state: dict[str, bool]) -> dict:
    require(shutil.which("git") is not None, "找不到git；未开始写入。")
    require(ROOT.is_dir() and PROGRAM.is_dir() and ADOPTIONS.is_dir(), "固定项目或交接父目录不存在。")
    initial_raw = regular_bytes(INITIAL)
    require(fingerprint(initial_raw) == INITIAL_SHA, "固定c8bf前像表字节改变。")
    initial = decode_map(initial_raw, 685)
    require(PRI in initial and BUILD in initial and CPP not in initial and HEADER not in initial,
            "冻结表与四文件范围不符。")
    candidate, manifest_raw = candidate_inputs()
    review_images = {}
    for name, expected_sha in REVIEWS.items():
        raw = regular_bytes(Path(__file__).resolve().parent / name)
        require(fingerprint(raw) == expected_sha, f"三期独审原件改变：{name}")
        review_images[name] = raw
    require_absent(OUT)
    require_absent(PROGRAM / CPP)
    require_absent(PROGRAM / HEADER)
    require_absent(TOOL_DIRECTORY)
    require(TOOL_DIRECTORY.parent.is_dir(), "05工具父目录不存在，拒绝扩大创建范围。")
    actual_before = actual_map(initial)
    require(actual_before == initial, "实际685全源不等于固定前像；未开始写入。")
    before = {name: regular_bytes(PROGRAM / name) for name in (PRI, BUILD)}
    after = build_postimages(before, candidate)
    expected = dict(initial)
    expected.update({name: fingerprint(raw) for name, raw in after.items()})
    require(len(expected) == 687, "预期全表不是687项。")
    patch_raw = make_patch(before, after)
    patch_sha = fingerprint(patch_raw)

    OUT.mkdir()  # 首次证据写入；以上全部是只读前置核对。
    run_state["out_created_by_this_run"] = True
    write_new(OUT / "source_before.json", initial_raw)
    save("actual_before.json", actual_before)
    save("expected_after.json", expected)
    write_new(OUT / "p04_after_manifest.json", manifest_raw)
    for name, raw in review_images.items():
        write_new(OUT / "reviews" / name, raw)
    for name, raw in before.items():
        write_new(OUT / "before/06_programs/sn100_host" / name, raw)
    for name, raw in after.items():
        write_new(OUT / "candidate/06_programs/sn100_host" / name, raw)
    patch = OUT / "observer_04.patch"
    write_new(patch, patch_raw)
    source_mapping = [{"source": str((P04 / "candidate" / source).relative_to(ROOT)),
                       "destination": "06_programs/sn100_host/" + destination,
                       "sha256": P04_HASHES[source]} for source, destination in SOURCE_MAPPING.items()]
    tool_mapping = [{"source": str((P04 / "candidate" / name).relative_to(ROOT)),
                     "destination": TOOL_RELATIVE + "/" + name,
                     "sha256": P04_HASHES[name], "executed": False} for name in TOOL_NAMES]
    save("prepared.json", {
        "schema": "sn100-lis-observer-source-installation-prepared-v1",
        "time_utc": datetime.now(timezone.utc).isoformat(), "initial_source_count": 685,
        "expected_source_count": 687, "initial_source_table_sha256": INITIAL_SHA,
        "patch_sha256": patch_sha, "new_source_mapping": source_mapping,
        "tool_mapping": tool_mapping, "review_sha256": REVIEWS,
        "changed_source_paths": sorted(after), "installer_actual_exit": None,
        "compiler_result": None, "rk_result": None, "application_exit": None,
    })

    # check、apply分别记录真实命令及退出；任何一步失败都保留原件并停止。
    require(actual_map(initial) == initial, "准备期间正式前像改变，拒绝check/apply。")
    for name in (CPP, HEADER):
        require_absent(PROGRAM / name)
    commands = []
    require(run_git("check", patch, commands) == 0, "git apply --check失败；未执行apply。")
    require(actual_map(initial) == initial, "check之后正式前像改变，拒绝apply。")
    for name in (CPP, HEADER):
        require_absent(PROGRAM / name)
    require_absent(TOOL_DIRECTORY)
    candidate_again, manifest_again = candidate_inputs()
    require(candidate_again == candidate and manifest_again == manifest_raw, "已审候选在准备期间改变。")
    require(regular_bytes(patch) == patch_raw, "已生成补丁在check后改变。")
    for name, raw in after.items():
        require(regular_bytes(OUT / "candidate/06_programs/sn100_host" / name) == raw,
                "独立后像在check后改变。")
    require(run_git("apply", patch, commands) == 0, "git apply失败；不覆盖修复或继续安装。")
    actual_after = actual_map(expected)
    save("source_after.json", actual_after)
    require(len(actual_after) == 687 and actual_after == expected,
            "实际687全源不等于预期；保留失败现场，不接受、不自动修复。")

    # 两份独立工具只复制精确字节，既有目录即拒绝；不运行其正文。
    require_absent(TOOL_DIRECTORY)
    TOOL_DIRECTORY.mkdir()
    for name in TOOL_NAMES:
        write_new(TOOL_DIRECTORY / name, candidate[name])
        require(fingerprint(regular_bytes(TOOL_DIRECTORY / name)) == P04_HASHES[name], "05工具复制后指纹不符。")
    require(actual_map(expected) == expected, "复制工具期间正式全源改变；不写接受结果。")
    acceptance = {
        "schema": "sn100-root-lis-observer-source-installation-v1",
        "time_utc": datetime.now(timezone.utc).isoformat(), "source_count": 687,
        "actual_source_equals_expected": True, "source_map_sha256": fingerprint(regular_bytes(OUT / "source_after.json")),
        "initial_source_table_sha256": INITIAL_SHA, "patch_sha256": patch_sha,
        "commands": commands, "review_sha256": REVIEWS,
        "new_source_mapping": source_mapping, "tool_mapping": tool_mapping,
        "validation_scope": "source_installation_and_exact_bytes_only",
        "installer_actual_exit": None, "compiler_result": None,
        "rk_result": None, "application_exit": None,
        "outer_runner_must_record_installer_actual_exit": True,
    }
    save("acceptance.json", acceptance)
    return acceptance


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="仅总控读审后显式执行源码安装")
    args = parser.parse_args()
    if not args.apply:
        parser.error("默认拒绝写入；本候选未执行。总控读审后方可显式使用 --apply。")
    run_state = {"out_created_by_this_run": False}
    try:
        result = install(run_state)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        failure = {"schema": "sn100-lis-observer-source-installation-failure-v1",
                   "reason": str(error), "acceptance_written_by_this_run":
                   run_state["out_created_by_this_run"] and (OUT / "acceptance.json").is_file(),
                   "installer_actual_exit": None, "compiler_result": None,
                   "rk_result": None, "application_exit": None,
                   "automatic_rollback_or_cleanup_performed": False}
        if run_state["out_created_by_this_run"] and OUT.is_dir() and not (OUT / "failure.json").exists():
            try:
                save("failure.json", failure)
            except OSError:
                pass
        print(json.dumps(failure, ensure_ascii=False, indent=2), file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
