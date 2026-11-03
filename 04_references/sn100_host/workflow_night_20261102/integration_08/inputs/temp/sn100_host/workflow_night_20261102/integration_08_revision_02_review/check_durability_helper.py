"""只核工具落盘语义：专用小文件，不读写正式源码，不执行晋级主流程。"""
from pathlib import Path
import ast
import hashlib
import json
import os
import subprocess
import sys

here = Path(__file__).resolve().parent
script = here / "promote_build_input_fix.py"
raw = script.read_bytes()
tree = ast.parse(raw, filename=str(script))
function = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "save_progress")
assignment = next(node for node in function.body if isinstance(node, ast.Assign)
                  and any(isinstance(target, ast.Name) and target.id == "helper" for target in node.targets))
helper = ast.literal_eval(assignment.value)
probe = here / "durability_probe_01"
probe.mkdir(exist_ok=False)
(probe / "fix_promotion_commands").mkdir(exist_ok=False)
path = probe / "probe.txt"
data = b"integration08 revision02 tool durability probe\n"
with path.open("xb") as output:
    output.write(data)
    output.flush()
    os.fsync(output.fileno())
before = hashlib.sha256(path.read_bytes()).hexdigest()
wsl_cwd = "/mnt/" + probe.drive[0].lower() + "/" + probe.as_posix()[3:]
argv = [str(Path(os.environ["SystemRoot"]) / "System32/wsl.exe"), "--distribution", "Ubuntu-24.04", "--user", "qyq",
        "--cd", wsl_cwd, "--exec", "/usr/bin/env", "-i", "HOME=/home/qyq", "USER=qyq", "LOGNAME=qyq",
        "PATH=/usr/bin:/bin", "LC_ALL=C", "/usr/bin/python3", "-c", helper, path.name, before]

def save_new(name, value):
    with (probe / name).open("xb") as output:
        output.write(value)
        output.flush()
        os.fsync(output.fileno())

save_new("command.json", (json.dumps({"argv": argv, "native_python": sys.executable,
                                     "promoter_sha256": hashlib.sha256(raw).hexdigest()}, indent=2) + "\n").encode())
result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
save_new("stdout.log", result.stdout)
save_new("stderr.log", result.stderr)
save_new("actual.exit", (str(result.returncode) + "\n").encode())
after = hashlib.sha256(path.read_bytes()).hexdigest()
record = {"format": "sn100-integration08-revision02-tool-durability-probe-v1", "actual_exit": result.returncode,
          "promoter_executed": False, "formal_modified": False, "product_executed": False,
          "native_file_fsync_completed": True, "same_helper_from_ast": True,
          "before_sha256": before, "after_sha256": after, "byte_equal": path.read_bytes() == data,
          "stdout_sha256": hashlib.sha256(result.stdout).hexdigest(), "stderr_sha256": hashlib.sha256(result.stderr).hexdigest()}
save_new("result.json", (json.dumps(record, indent=2) + "\n").encode())
print(json.dumps(record, ensure_ascii=False))
if result.returncode != 0 or before != after or not record["byte_equal"]:
    raise SystemExit(2)
