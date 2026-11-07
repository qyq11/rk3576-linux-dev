"""只检查和制作源码候选，不编译或运行SN100。"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

candidate = Path(__file__).resolve().parent
repo = candidate.parents[2]
formal = repo / "06_programs/sn100_host"
baseline_file = repo / "07_backups/sn100_host/board_a/formal_merge_20261104_01/application/source_snapshot.json"
relative = "instrument/instrument_page.cpp"

def sha(data):
    return hashlib.sha256(data).hexdigest()

def save_json(name, value):
    (candidate / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

def read_formal():
    return {name: sha((formal / name).read_bytes()) for name in expected}

expected = json.loads(baseline_file.read_text(encoding="utf-8-sig"))
assert len(expected) == 697
before_table = read_formal()
save_json("formal_before.json", before_table)
assert before_table == expected, "正式源码与697源归档不一致，停止制作候选"

source = (formal / relative).read_bytes()
text = source.decode("utf-8")
assert "\r\n" in text and text.replace("\r\n", "").count("\n") == 0

def replace_once(old, new):
    global text
    assert text.count(old) == 1, old
    text = text.replace(old, new, 1)

replace_once("#include <QComboBox>\r\n", "#include <QComboBox>\r\n#include <QColor>\r\n")
replace_once("#include <QTextDocument>\r\n", "#include <QTextDocument>\r\n#include <QTextCharFormat>\r\n")
replace_once("void appendCommunicationLine(QTextBrowser *view, const QString &line)\r\n",
             "void appendCommunicationLine(QTextBrowser *view, const QString &line, int timestampLength=0)\r\n")
replace_once("    cursor.insertText(line);\r\n", """    const QTextCharFormat bodyFormat=cursor.charFormat();
    if (timestampLength>0)
    {
        // 原收发时间前缀为darkgreen；正文仍用纯文本和原格式，不解释设备中的HTML。
        QTextCharFormat timestampFormat=bodyFormat;
        timestampFormat.setForeground(QColor(QStringLiteral("darkgreen")));
        cursor.insertText(line.left(timestampLength),timestampFormat);
        cursor.insertText(line.mid(timestampLength),bodyFormat);
    }
    else cursor.insertText(line,bodyFormat);
""".replace("\n", "\r\n"))
replace_once("    const QString line=QDateTime::currentDateTime().toString(QStringLiteral(\"HH:mm:ss:zzz\")) +\r\n",
             "    const QString timestamp=QDateTime::currentDateTime().toString(QStringLiteral(\"HH:mm:ss:zzz\"));\r\n    const QString line=timestamp +\r\n")
replace_once("    appendCommunicationLine(view,line);\r\n", "    appendCommunicationLine(view,line,timestamp.size());\r\n")
after = text.encode("utf-8")

for prefix, data in (("before", source), ("tree", after)):
    path = candidate / prefix / relative
    path.parent.mkdir(parents=True, exist_ok=False)
    path.write_bytes(data)

env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(candidate))
diff = subprocess.run(["git", "diff", "--no-index", "--binary", "--no-ext-diff", "--", "before/" + relative, "tree/" + relative],
                      cwd=candidate, env=env, capture_output=True)
assert diff.returncode == 1
patch = diff.stdout.replace(b"a/before/", b"a/").replace(b"b/tree/", b"b/")
(candidate / "timestamp_colour.patch").write_bytes(patch)
(candidate / "diff_stderr.txt").write_bytes(diff.stderr)

private = candidate / "check_apply"
(private / relative).parent.mkdir(parents=True, exist_ok=False)
(private / relative).write_bytes(source)
results = []
for flags in (("--check",), ()):
    command = ["git", "apply", *flags, str(candidate / "timestamp_colour.patch")]
    result = subprocess.run(command, cwd=private, env=env, capture_output=True)
    results.append({"command": command, "exit_code": result.returncode,
                    "stdout": result.stdout.decode("utf-8", errors="replace"),
                    "stderr": result.stderr.decode("utf-8", errors="replace")})
    save_json("patch_results.json", results)
    assert result.returncode == 0
assert (private / relative).read_bytes() == after

# 正式源仅检查补丁可应用性，不应用。Git的--directory从仓库根解释路径。
formal_check = subprocess.run(["git", "apply", "--check", "--directory=06_programs/sn100_host",
                               str(candidate / "timestamp_colour.patch")], cwd=repo, capture_output=True)
results.append({"command": ["git", "apply", "--check", "--directory=06_programs/sn100_host", str(candidate / "timestamp_colour.patch")],
                "exit_code": formal_check.returncode,
                "stdout": formal_check.stdout.decode("utf-8", errors="replace"),
                "stderr": formal_check.stderr.decode("utf-8", errors="replace")})
save_json("patch_results.json", results)
assert formal_check.returncode == 0
after_table = read_formal()
save_json("formal_after.json", after_table)
assert after_table == before_table

inputs = [relative, "instrument/instrument_page.h", "instrument/original_channel_receiver.h",
          "instrument/original_channel_receiver.cpp", "shell/device_control_shell.cpp",
          "shell/frmmain.h", "shell/main.cpp"]
save_json("selected_inputs.json", {name: before_table[name] for name in inputs})
reference = repo / "06_programs/sn100_host_reference/Seerain450_PC"
save_json("original_inputs.json", {name: sha((reference / name).read_bytes()) for name in
                                   ("frmdriverset/frmdriverset.cpp", "frmdriverset/frmdriverset.ui")})
save_json("changed_files.json", [{"path": relative, "before_sha256": sha(source), "after_sha256": sha(after),
                                   "patch_sha256": sha(patch)}])
save_json("source_check_summary.json", {
    "formal_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo).decode().strip(),
    "baseline_source": str(baseline_file.relative_to(repo)), "baseline_table_sha256": sha(baseline_file.read_bytes()),
    "formal_files_read_before": len(before_table), "formal_files_read_after": len(after_table),
    "formal_equals_baseline": before_table == expected, "formal_unchanged": after_table == before_table,
    "private_apply_equals_tree": True, "patch_check_apply_exit_codes": [r["exit_code"] for r in results],
    "diff_no_index_exit_code": diff.returncode, "crlf_preserved": after.replace(b"\r\n", b"").count(b"\n") == 0,
    "product_build_or_execution": False, "board_connection": False})
print("源码候选制作完成：697源前后相同，私有检查/应用0/0，正式只检查0；未编译或运行产品。")
