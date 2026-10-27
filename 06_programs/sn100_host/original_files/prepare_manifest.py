"""电脑端只读核对待复制原文件；输出路径/指纹，不输出配置或数据库记录。"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath

parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--inventory", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
assert args.source.is_absolute() and args.output.is_absolute()
source = args.source.resolve(strict=True)
inventory = json.loads(args.inventory.read_text(encoding="utf-8-sig"))
selected = []
allowed = {".db", ".ini", ".cfg", ".json", ".csv", ".png", ".gif", ".ico", ".xml", ".qm"}
for item in inventory["files"]:
    name = PurePosixPath(item["path"])
    if name.suffix.lower() not in allowed:
        continue
    assert not name.is_absolute() and ".." not in name.parts
    path = source.joinpath(*name.parts)
    assert path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(source)
    for parent in path.parents:
        assert not parent.is_symlink()
        if parent == source:
            break
    # 原参考包必须是静态、已关闭的资料；有写日志残留时不做普通文件复制。
    if name.suffix.lower() == ".db":
        for suffix in ("-wal", "-journal", "-shm"):
            assert not Path(str(path) + suffix).exists(), f"Database sidecar present: {name}{suffix}"
    stat = path.stat()
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    assert digest == item["sha256"] and stat.st_size == item["bytes"], f"Source changed: {name}"
    selected.append({"path": name.as_posix(), "bytes": stat.st_size, "sha256": digest})
assert len(selected) == 141 and sum(p["path"].endswith(".db") for p in selected) == 29
args.output.mkdir(parents=True, exist_ok=True)
manifest = {"format": "sn100-original-files-v1", "source": "Seerain450_PC/bin", "files": selected,
            "fileCount": len(selected), "databaseCount": 29,
            "scope": "原文件工作副本；未激活旧通信、账户或患者记录；DB不转换、不加表"}
(args.output / "original_manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
(args.output / "original.SHA256SUMS").write_text("".join(f'{p["sha256"]}  {p["path"]}\n' for p in selected), encoding="utf-8")
(args.output / "filelist.nul").write_bytes(b"".join(p["path"].encode("utf-8") + b"\0" for p in selected))
print(f"ORIGINAL_FILES={len(selected)} DATABASES=29 SOURCE_HASHES_MATCH=1")
