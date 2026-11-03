"""核对整程序构建清单中的源码、表单和资源均进入同批指纹清单。"""
from pathlib import Path
import hashlib
import json

root = Path(__file__).resolve().parents[3]
program = root / "06_programs/sn100_host"
work = Path(__file__).resolve().parent / "integration_04"
snapshot = json.loads((work / "source_snapshot.json").read_text(encoding="utf-8"))
checked = set()
counts = {}
for line in (work / "build_inputs.tsv").read_text(encoding="utf-8").splitlines():
    kind, value = line.split("\t", 1)
    if kind == "include":
        continue
    if kind not in ("source", "form", "header", "resource", "helper_source"):
        raise RuntimeError("构建清单出现未知类型：" + kind)
    path = Path(value)
    relative = path.relative_to(program).as_posix()
    if relative not in snapshot or hashlib.sha256(path.read_bytes()).hexdigest() != snapshot[relative]:
        raise RuntimeError("构建输入未被本批指纹清单完整保存：" + relative)
    checked.add(relative)
    counts[kind] = counts.get(kind, 0) + 1
if counts.get("source", 0) < 100:
    raise RuntimeError("整程序源码清单不完整，未开始编译。")
print("BUILD_INPUTS_COVERED_BY_SOURCE_SNAPSHOT=" + str(len(checked)))
print(json.dumps(counts, ensure_ascii=False, sort_keys=True))
