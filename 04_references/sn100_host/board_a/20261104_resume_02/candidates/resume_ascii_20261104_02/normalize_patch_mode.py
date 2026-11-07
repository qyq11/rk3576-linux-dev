"""按Git中实际100644模式补正补丁头；保留WSL挂载模式的首轮记录。"""
import hashlib
import json
import os
from pathlib import Path
import subprocess

base = Path(__file__).resolve().parent
repo = base.parents[2]
relative = "instrument/instrument_page.cpp"
patch_file = base / "timestamp_colour.patch"
old_patch = patch_file.read_bytes()
assert old_patch.count(b" 100755\n") == 1
stage = subprocess.check_output(["git", "ls-files", "--stage", "--", "06_programs/sn100_host/" + relative], cwd=repo)
assert stage.startswith(b"100644 ")
(base / "raw_mount_mode.patch").write_bytes(old_patch)
(base / "patch_results_initial.json").write_bytes((base / "patch_results.json").read_bytes())
patch = old_patch.replace(b" 100755\n", b" 100644\n", 1)
patch_file.write_bytes(patch)

private = base / "check_apply_normalized"
(private / relative).parent.mkdir(parents=True, exist_ok=False)
(private / relative).write_bytes((base / "before" / relative).read_bytes())
env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(base))
results = []
for cwd, command in ((private, ["git", "apply", "--check", str(patch_file)]),
                     (private, ["git", "apply", str(patch_file)]),
                     (repo, ["git", "apply", "--check", "--directory=06_programs/sn100_host", str(patch_file)])):
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True)
    results.append({"command": command, "exit_code": result.returncode,
                    "stdout": result.stdout.decode("utf-8", errors="replace"),
                    "stderr": result.stderr.decode("utf-8", errors="replace")})
    (base / "patch_results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    assert result.returncode == 0
assert (private / relative).read_bytes() == (base / "tree" / relative).read_bytes()
changes = json.loads((base / "changed_files.json").read_text(encoding="utf-8"))
changes[0]["patch_sha256"] = hashlib.sha256(patch).hexdigest()
(base / "changed_files.json").write_text(json.dumps(changes, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
(base / "patch_mode_note.json").write_text(json.dumps({
    "git_index_stage": stage.decode().strip(),
    "original_patch_sha256": hashlib.sha256(old_patch).hexdigest(),
    "final_patch_sha256": hashlib.sha256(patch).hexdigest(),
    "only_patch_header_mode_changed": True,
    "after_source_unchanged": True,
    "private_check_apply_and_formal_check": [r["exit_code"] for r in results],
    "final_stderr_empty": all(not r["stderr"] for r in results)}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("补丁模式已按Git的100644补正；源码不变，三项实际0，最终无模式警告。")
