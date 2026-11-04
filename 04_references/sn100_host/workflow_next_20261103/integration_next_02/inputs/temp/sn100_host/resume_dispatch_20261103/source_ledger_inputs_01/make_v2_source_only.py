"""只生成并解析v2源码；不import、不调用任何来源聚合器。"""
from pathlib import Path
import ast
import difflib
import hashlib

folder = Path(__file__).resolve().parent
source = folder.parent / "source_ledger_candidate.py"
target = folder / "source_ledger_candidate_v2.py"
before = source.read_bytes()
assert hashlib.sha256(before).hexdigest() == "738fa974c4fb74e5de82485d423bf91995571f9b0022570330d6d15286118ac7"
newline = b"\r\n" if b"\r\n" in before else b"\n"
anchor = b'        acceptance = read_record(item["acceptance"])' + newline
assert before.count(anchor) == 1
insert = (
    b'        supporting = item.get("supporting_records")\n'
    b'        if supporting is not None:\n'
    b'            need(isinstance(supporting, list) and supporting, "supporting_records must be a nonempty record list.")\n'
    b'            evidence["supporting_records"] = [checked(rec) for rec in supporting]\n'
).replace(b"\n", newline)
root_anchor = b"ROOT = Path(__file__).resolve().parents[3]"
assert before.count(root_anchor) == 1
after = before.replace(anchor, insert + anchor).replace(root_anchor, b"ROOT = Path(__file__).resolve().parents[4]")
ast.parse(after, filename=str(target))
assert not target.exists()
target.write_bytes(after)
diff = "".join(difflib.unified_diff(before.decode("utf-8").splitlines(keepends=True),
    after.decode("utf-8").splitlines(keepends=True),
    fromfile="source_ledger_candidate.py", tofile="source_ledger_candidate_v2.py"))
(folder / "source_ledger_v2_only_supporting_records.diff").write_bytes(diff.encode("utf-8"))
print("V2_SOURCE_AST_ONLY " + hashlib.sha256(after).hexdigest())
