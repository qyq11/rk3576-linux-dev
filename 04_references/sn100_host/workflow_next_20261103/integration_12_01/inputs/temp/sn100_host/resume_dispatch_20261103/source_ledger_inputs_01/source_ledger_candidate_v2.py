"""仅聚合Root已执行的源码接纳证据；不执行补丁、构建、产品或板卡动作。

这是未执行候选。缺完整且冻结的Root spec即拒绝，所有输出只允许新建于temp。
applied表示原Root脚本已实际应用且精确后像已核，不表示本聚合器执行过apply。
"""
from pathlib import Path, PurePosixPath
from datetime import datetime, timezone
import argparse
import ast
import hashlib
import json
import os
import re
import stat

ROOT = Path(__file__).resolve().parents[4]
PROGRAM = ROOT / "06_programs/sn100_host"
SHA = re.compile(r"[a-f0-9]{64}\Z")
REVIEW_FORMAT = "sn100-root-real-source-final-review-v1"
PROGRESS_FORMAT = "sn100-root-real-source-promotion-v1"
INPUTS = {}


def need(condition, message):
    if not condition:
        raise RuntimeError(message)


def relative(name):
    need(isinstance(name, str) and name and "\\" not in name and ":" not in name
         and not PurePosixPath(name).is_absolute()
         and not any(ord(c) < 32 for c in name)
         and all(p not in ("", ".", "..") for p in name.split("/")), "不安全相对路径。")
    return name


def ordinary(path, exists=True):
    path = Path(path)
    path.resolve().relative_to(ROOT.resolve())
    for item in (path, *path.parents):
        need(not item.is_symlink() and not (hasattr(item, "is_junction") and item.is_junction()),
             "路径含链接：" + str(item))
        if item == ROOT:
            break
    if exists:
        info = path.stat()
        need(stat.S_ISREG(info.st_mode) and info.st_nlink == 1, "必须为单链接普通文件：" + str(path))
    return path


def digest(path):
    value = hashlib.sha256()
    with ordinary(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            value.update(block)
    return value.hexdigest()


def record(path):
    path = ordinary(path)
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": digest(path), "bytes": path.stat().st_size}


def checked(item):
    need(isinstance(item, dict) and isinstance(item.get("sha256"), str)
         and SHA.fullmatch(item["sha256"]), "必要record缺失或SHA未冻结。")
    path = ordinary(ROOT / relative(item["path"]))
    actual = record(path)
    need(actual["sha256"] == item["sha256"] and
         ("bytes" not in item or actual["bytes"] == item["bytes"]), "输入已漂移：" + item["path"])
    previous = INPUTS.setdefault(actual["path"], actual)
    need(previous == actual, "同一输入出现不同版本。")
    return actual


def read_record(item):
    return json.loads((ROOT / checked(item)["path"]).read_text(encoding="utf-8-sig"))


def source_map(item):
    value = read_record(item)
    need(isinstance(value, dict) and value, "缺完整源码表。")
    for name, sha in value.items():
        relative(name)
        need(isinstance(sha, str) and SHA.fullmatch(sha), "源码表SHA无效。")
    return value


def records_in(value):
    if isinstance(value, dict):
        if "path" in value and "sha256" in value:
            yield value
        else:
            for child in value.values():
                yield from records_in(child)
    elif isinstance(value, list):
        for child in value:
            yield from records_in(child)


def changed(before, after):
    return sorted(n for n in before.keys() & after.keys() if before[n] != after[n]), \
        sorted(after.keys() - before.keys()), sorted(before.keys() - after.keys())


def exit_is(value, key, expected):
    return type(value.get(key)) is int and value[key] == expected


def portable_path(value):
    value = value.replace("\\", "/")
    if value.startswith("/mnt/c/"):
        value = "C:/" + value[7:]
    return value.lower() if re.match(r"^[A-Za-z]:/", value) else value


def command_evidence(item, phase, patch):
    result_record = checked(item["result"])
    value = read_record(result_record)
    stdout, stderr = checked(item["stdout"]), checked(item["stderr"])
    need(value.get("phase") == phase and exit_is(value, "actual_exit", 0), "没有原git检查/应用的实际成功记录。")
    argv = value.get("argv")
    need(isinstance(argv, list) and len(argv) >= 3 and all(isinstance(x, str) for x in argv)
         and argv[0].replace("\\", "/").rsplit("/", 1)[-1] in ("git", "git.exe") and "apply" in argv,
         "不是实际git apply命令原件。")
    need(("--check" in argv) == (phase == "check") and
         not any(x.startswith("--whitespace=fix") or x in ("--3way", "--ignore-whitespace", "--ignore-space-change") for x in argv),
         "未审的模糊、三方或自动修补选项。")
    need(portable_path(argv[-1]) == portable_path((ROOT / patch["path"]).as_posix()), "命令不指向本片实际补丁。")
    return {"result": result_record, "stdout": stdout, "stderr": stderr, "recorded_argv": argv}


def exact_line_ending_correction(item, failed, expected, final, acceptance, history):
    need(item.get("kind") == "exact-crlf-postimage-v1", "未知补正类型须另审。")
    first_record, inspection_record = checked(item["first_entry_result"]), checked(item["inspection"])
    first, inspection = read_record(first_record), read_record(inspection_record)
    paths = item["paths"]
    need(isinstance(paths, list) and paths and paths == sorted(set(paths)), "补正范围未由Root明确冻结。")
    allowed = {relative(p) for p in paths}
    need(exit_is(first, "actual_exit", 1) and exit_is(first, "git_check_actual_exit", 0)
         and exit_is(first, "git_apply_actual_exit", 0)
         and set(first.get("mismatch_paths", [])) == allowed
         and first.get("original_comparison_and_logs_preserved") is True, "原片首外层失败原件缺失。")
    need(isinstance(inspection, list) and {x.get("path") for x in inspection} == allowed
         and all(x.get("equal_after_crlf_to_lf") is True and x.get("current_equals_failed_snapshot") is True for x in inspection),
         "没有已核行尾差异的原检查。")
    actual_diff = set().union(*map(set, changed(failed, final)))
    need(actual_diff == allowed and final == expected, "补正改变了Root批准范围之外的后像。")
    for row in inspection:
        need(row.get("actual") == failed[row["path"]] and row.get("expected") == expected[row["path"]],
             "原行尾检查没有对应失败/批准的准确SHA。")
    before_bytes, approved_bytes = {}, {}
    for source, target in ((item["backups"], before_bytes), (item["approved_postimages"], approved_bytes)):
        for entry in source:
            name = relative(entry["relative_file"])
            need(name not in target, "重复补正原件。")
            rec = checked(entry["record"])
            need(rec["sha256"] == (failed[name] if target is before_bytes else expected[name]), "补正字节原件SHA不符。")
            target[name] = (ROOT / rec["path"]).read_bytes()
    need(set(before_bytes) == allowed == set(approved_bytes), "缺补正前原件或批准字节。")
    need(all(before_bytes[n].replace(b"\r\n", b"\n") == approved_bytes[n].replace(b"\r\n", b"\n") for n in allowed),
         "补正不仅是原件/批准后像的CRLF/LF差异。")
    need(exit_is(acceptance, "first_entry_actual_exit", 1) and exit_is(acceptance, "correction_actual_exit", 0)
         and set(acceptance.get("line_ending_correction", [])) == allowed, "缺实际补正成功记录。")
    checked(item["script"])
    for rec in (item["failed_checkpoint"], first_record, inspection_record):
        history.append(checked(rec))


def inventory(script_record):
    """只解析官方snapshot源码；不import或运行其函数，未知规则拒绝。"""
    path = ROOT / checked(script_record)["path"]
    tree = ast.parse(path.read_bytes(), filename=str(path))
    nodes = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "snapshot"]
    need(len(nodes) == 1, "缺唯一官方snapshot函数。")
    function = nodes[0]
    tuples = sorted((n for n in ast.walk(function) if isinstance(n, ast.Tuple)), key=lambda n: (n.lineno, n.col_offset))
    need(len(tuples) == 4, "snapshot规则改变，须先独立审查。")
    values = [ast.literal_eval(n) for n in tuples]
    need(all(isinstance(v, tuple) and v and all(isinstance(x, str) for x in v) for v in values)
         and values[1] == (".md", ".pyc"), "未知snapshot清单或排除规则。")
    template = '''def snapshot(program):
    paths = []
    for name in ("DIR",):
        paths.extend(p for p in (program / name).rglob("*")
                     if p.is_file() and p.suffix not in (".md", ".pyc")
                     and "__pycache__" not in p.parts)
    paths.extend(program / name for name in ("FILE",))
    paths.extend(program / 'protocol' / name for name in ("PROTOCOL",))
    return {str(p.relative_to(program)): sha(p) for p in sorted(set(paths))}
'''
    class Normalize(ast.NodeTransformer):
        def visit_Tuple(self, node):
            return ast.copy_location(ast.Tuple(elts=[], ctx=ast.Load()), node)
    need(ast.dump(Normalize().visit(function), include_attributes=False) ==
         ast.dump(Normalize().visit(ast.parse(template).body[0]), include_attributes=False),
         "snapshot控制规则改变，未猜测完整范围。")
    directories, _, files, protocol_files = values
    paths = set()
    for name in directories:
        directory = PROGRAM / relative(name)
        need(directory.is_dir() and not directory.is_symlink(), "源码范围目录缺失或为链接。")
        for child in directory.rglob("*"):
            need(not child.is_symlink() and not (hasattr(child, "is_junction") and child.is_junction()), "源码范围含链接。")
            if child.is_file() and child.suffix not in (".md", ".pyc") and "__pycache__" not in child.parts:
                paths.add(child)
    paths.update(PROGRAM / relative(n) for n in files)
    paths.update(PROGRAM / "protocol" / relative(n) for n in protocol_files)
    return {p.relative_to(PROGRAM).as_posix(): digest(p) for p in sorted(paths)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root-spec", required=True, type=Path)
    args = parser.parse_args()
    spec_record = record(ordinary(args.root_spec.resolve()))
    spec = read_record(spec_record)
    need(spec.get("format") == "sn100-root-source-ledger-spec-v1" and spec.get("frozen") is True
         and spec.get("owner") == "root" and spec.get("all_intended_source_slices_present") is True,
         "缺完整Root冻结spec；未创建任何终态。")
    self_record = checked(spec["ledger_script"])
    need(self_record == record(Path(__file__).resolve()), "不是Root批准的聚合器原件。")
    checked(spec["root_approval"])
    baseline_record, archive_record = checked(spec["baseline_snapshot"]), checked(spec["baseline_archive"])
    baseline, final = source_map(baseline_record), source_map(spec["final_source_manifest"])
    attributes = checked(spec["root_attributes"])
    live_attributes = checked(record(ROOT / ".gitattributes"))
    need(attributes["sha256"] == live_attributes["sha256"], "当前根属性不等于冻结实际属性原件。")
    git_config = spec["git_apply_config"]
    need(isinstance(git_config, dict) and git_config and list(records_in(git_config)), "缺实际Git配置来源record。")
    for rec in records_in(git_config):
        checked(rec)
    patches = spec["patches"]
    names = spec["required_patch_names"]
    need(isinstance(patches, list) and patches and isinstance(names, list) and len(set(names)) == len(names)
         and [x.get("name") for x in patches] == names, "仍有计划片缺失或顺序未固定。")
    history = [checked(x) for x in spec["historical_failed_checks"]]
    previous, entries, recorded = baseline, [], []
    for index, item in enumerate(patches, 1):
        need(item.get("index") == index, "真实补丁编号必须从1连续。")
        patch = checked(item["patch"])
        before, after = source_map(item["before_checkpoint"]), source_map(item["after_checkpoint"])
        expected = source_map(item["expected_after"])
        need(before == previous and after == expected and before != after, "真实片链不连续或精确后像未成立。")
        scope = sorted(set().union(*map(set, changed(before, after))))
        need(scope == item["scope"], "本片实际范围不等于Root明确批准范围。")
        reviews, scripts = item["reviews"], item["adoption_scripts"]
        need(reviews and scripts, "缺独立审查或原实际接纳脚本。")
        evidence = {"reviews": [checked(x) for x in reviews], "adoption_scripts": [checked(x) for x in scripts],
                    "check": command_evidence(item["check"], "check", patch),
                    "apply": command_evidence(item["apply"], "apply", patch),
                    "acceptance": checked(item["acceptance"]), "expected_after": checked(item["expected_after"])}
        supporting = item.get("supporting_records")
        if supporting is not None:
            need(isinstance(supporting, list) and supporting, "supporting_records must be a nonempty record list.")
            evidence["supporting_records"] = [checked(rec) for rec in supporting]
        acceptance = read_record(item["acceptance"])
        need(acceptance.get("actual_source_equals_expected") is True and acceptance.get("source_files") == len(after),
             "缺Root实际后像接纳结果。")
        if item.get("correction") is not None:
            correction = item["correction"]
            need(set(correction["paths"]) <= set(scope), "行尾补正越出本片批准范围。")
            failed = source_map(correction["failed_checkpoint"])
            exact_line_ending_correction(correction, failed, expected, after, acceptance, history)
            need(acceptance.get("source_map_sha256") == checked(item["after_checkpoint"])["sha256"],
                 "补正接纳没有绑定实际最终after。")
            evidence["correction"] = correction
        else:
            need(acceptance.get("patch_sha256") == patch["sha256"], "接纳记录没有绑定本片补丁。")
        entries.append({"index": index, "name": item["name"], "patch": patch["path"], "sha256": patch["sha256"],
                        "before_checkpoint": checked(item["before_checkpoint"]),
                        "after_checkpoint": checked(item["after_checkpoint"]), "scope": scope, "actual_evidence": evidence})
        recorded.append({"index": index, "check": evidence["check"], "apply": evidence["apply"]})
        previous = after
    need(previous == final, "最后实际after不等于本批最终源码表。")
    actual = inventory(spec["snapshot_rules"])
    need(actual == final, "当前全源码集合/字节不等于最终后像。")
    unique_history = {(x["path"], x["sha256"]): x for x in history}
    history = list(unique_history.values())
    delta = dict(zip(("changed_files", "added_files", "deleted_files"), changed(baseline, final)))
    review = {"format": REVIEW_FORMAT, "created_at": datetime.now(timezone.utc).isoformat(),
              "aggregation_only": True, "executed_git_apply": False,
              "application_execution_owner": "root_original_adoption_scripts",
              "root_spec": spec_record, "root_approval": checked(spec["root_approval"]),
              "finalizer_script": self_record, "baseline_archive": archive_record,
              "baseline_snapshot": baseline_record, "final_source_manifest": checked(spec["final_source_manifest"]),
              "snapshot_rules": checked(spec["snapshot_rules"]), "source_count": len(final),
              "base_source_files": baseline, "final_source_files": final, "patches": entries,
              "git_apply_config": {"declared_actual_sources": git_config, "recorded_commands": recorded},
              "root_attributes": attributes, "actual_root_attributes": live_attributes,
              "historical_failed_checks": history, **delta}
    for rec in records_in(review):
        checked(rec)
    output = ROOT / relative(spec["output_directory"])
    output.relative_to(ROOT / "temp/sn100_host")
    ordinary(output, exists=False)
    need(output.parent.is_dir() and not output.exists(), "输出必须是既有temp父目录下的新目录。")
    need(all(not (ROOT / p).is_relative_to(output) for p in INPUTS), "输入不得反向包含未来输出。")
    need(inventory(spec["snapshot_rules"]) == final, "写入前当前全源再次核对失败。")
    for rec in list(INPUTS.values()):
        checked(rec)
    # 到这里才可写新文件；没有完整Root spec时连输出目录也不创建。
    output.mkdir()
    def save(name, value):
        data = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
        with (output / name).open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    save("verified_final_source.json", actual)
    save("final_review.json", review)
    review_record = record(output / "final_review.json")
    need(inventory(spec["snapshot_rules"]) == final, "review写入后源码漂移；保留原件，不写complete。")
    for rec in list(INPUTS.values()):
        checked(rec)
    progress = {"format": PROGRESS_FORMAT, "complete": True, "phase": "complete", "current_patch": None,
                "aggregation_only": True, "executed_git_apply": False,
                "preflight_evidence_origin": "recorded_original_git_apply_check",
                "base_source_files": baseline, "final_source_files": final, "patches": entries,
                "applied": [{k: p[k] for k in ("index", "patch", "sha256")} for p in entries],
                "preflight_applied": [p["index"] for p in entries], "verified_source_paths": len(final),
                "source_snapshot_sha256": baseline_record["sha256"],
                "final_merge_review_sha256": review_record["sha256"], "merge_review_sha256": review_record["sha256"],
                "promotion_script_sha256": self_record["sha256"], "git_apply_config": review["git_apply_config"],
                "root_attributes": attributes, "review_record": review_record, **delta}
    # cfg最后才冻结。此列表仅列来源证据，不反向修改review/progress或构建终态。
    save("required_cfg_evidence.json", sorted(INPUTS.values(), key=lambda r: r["path"]))
    save("promotion_progress.json", progress)
    print(json.dumps({"review": review_record, "promotion": record(output / "promotion_progress.json"),
                      "source_paths_verified": len(final), "executed_git_apply": False}, ensure_ascii=False))


if __name__ == "__main__":
    main()
