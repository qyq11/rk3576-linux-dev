"""核对源码边界并在私有三文件副本重放普通补丁；不编译或运行产品。"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

work = Path(__file__).resolve().parent
repo = work.parents[2]
package = work / "packaging_01"
assert not package.exists()
package.mkdir()
prefix = Path("06_programs/sn100_host")
names = ["samples/sample_types.h", "samples/original_sample_repository.h", "samples/original_sample_repository.cpp"]
paths = [str(prefix / name) for name in names]
git = ["/usr/bin/git", "-c", "core.autocrlf=false", "-c", "core.safecrlf=false", "-c", "core.whitespace=cr-at-eol"]
steps = []

def run(label, cwd, arguments, expected=0):
    command = git + arguments
    result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (package / (label + ".stdout")).write_bytes(result.stdout)
    (package / (label + ".stderr")).write_bytes(result.stderr)
    (package / (label + ".exit")).write_text(str(result.returncode) + "\n", encoding="utf-8")
    steps.append({"label": label, "cwd": str(cwd.relative_to(work)), "command": command, "actualExit": result.returncode})
    assert result.returncode == expected, (label, result.returncode, result.stderr.decode(errors="replace"))
    return result.stdout

def stage(directory, view):
    directory.mkdir()
    shutil.copyfile(repo / ".gitattributes", directory / ".gitattributes")
    for path in paths:
        target = directory / path
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(work / view / path, target)

builder = package / "patch_builder"
stage(builder, "base")
run("00_builder_init", builder, ["init", "--quiet"])
run("01_attributes", builder, ["check-attr", "text", "eol", "filter", "--"] + paths)
run("02_index_base", builder, ["add", "--"] + paths)
for path in paths:
    shutil.copyfile(work / "candidate" / path, builder / path)
patch = run("03_diff", builder, ["diff", "--no-ext-diff", "--no-color", "--"] + paths)
patch_name = package / "lis_B1_snapshot_auto_id.patch"
patch_name.write_bytes(patch)
assert len([line for line in patch.splitlines() if line.startswith(b"diff --git ")]) == 3
run("04_whitespace", builder, ["diff", "--check", "--"] + paths)

replay = package / "replay"
stage(replay, "base")
run("05_replay_init", replay, ["init", "--quiet"])
run("06_plain_check", replay, ["apply", "--check", str(patch_name)])
run("07_plain_apply", replay, ["apply", str(patch_name)])
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
replayed = {name: sha(replay / prefix / name) for name in names}
after = {name: sha(work / "candidate" / prefix / name) for name in names}
assert replayed == after
for path in paths:
    data = (replay / path).read_bytes()
    assert data.count(b"\n") == data.count(b"\r\n")

operations = json.loads((work / "edit_operations.json").read_text(encoding="utf-8"))
inverse = {}
for name in names:
    data = (work / "candidate" / prefix / name).read_bytes()
    for operation in reversed(operations):
        if operation["file"] != Path(name).name:
            continue
        old = operation["old"].replace("\n", "\r\n").encode("utf-8")
        new = operation["new"].replace("\n", "\r\n").encode("utf-8")
        if new:
            assert data.count(new) == 1
            data = data.replace(new, old)
        else:
            # 唯一删除的是旧私有映射声明，仍在同一相邻声明之间恢复。
            anchor = b"    bool originalLisProjectSource(QString *digest, QStringList *files, QString *message) const;\r\n"
            assert data.count(anchor) == 1
            data = data.replace(anchor, anchor + old)
    assert data == (work / "base" / prefix / name).read_bytes(), name
    inverse[name] = "exactBaseBytes"

baseline = json.loads((work / "before_source_manifest.json").read_text(encoding="utf-8-sig"))
formal = {name: sha(repo / prefix / name) for name in baseline}
assert formal == baseline
references = [
    "06_programs/sn100_host_reference/Seerain450_PC/customwidgets/cussamplehole.h",
    "06_programs/sn100_host_reference/Seerain450_PC/frmsample/frmsamplematerial.cpp",
    "06_programs/sn100_host_reference/Seerain450_PC/commlis/liscommsetting.cpp",
    "06_programs/sn100_host_reference/Seerain450_PC/commlis/liscommreceiver.cpp",
    "06_programs/sn100_host_reference/Seerain450_PC/frmsystem/frmsystemmaterial.cpp",
    "06_programs/sn100_host_reference/Seerain450_PC/customtools/sqlbase.cpp",
    "06_programs/sn100_host_reference/Seerain450_PC/tool/database/report/report_table.h",
    "06_programs/sn100_host/lis_business/original_lis_application_plan.h",
    "06_programs/sn100_host/lis_business/original_lis_application_plan.cpp",
    "06_programs/sn100_host/original_files/original_business_database.cpp",
]
(work / "read_basis.json").write_text(json.dumps({path: sha(repo / path) for path in references}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
(work / "after_target_manifest.json").write_text(json.dumps(after, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
(work / "before_target_manifest.json").write_text(json.dumps({name: sha(work / "base" / prefix / name) for name in names}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
(package / "result.json").write_text(json.dumps({
    "actualExit": 0, "steps": steps, "patchSha256": sha(patch_name), "replayedFiles": replayed,
    "replayMismatch": 0, "lineEnding": "CRLF", "inverseEditByteComparison": inverse,
    "formalPhysicalRead": len(formal), "formalMismatch": 0,
    "verificationScope": "sourceBytesAndPrivateGitOnly;NoCompileSqlProductNetworkOrBoard",
}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps({"actualExit": 0, "steps": [s["actualExit"] for s in steps], "changed": 3, "replayMismatch": 0, "formalMismatch": 0, "patchSha256": sha(patch_name)}))
