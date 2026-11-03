"""固定本轮已完成构建的程序及逐字节匹配源码，不复制运行数据库。"""
from pathlib import Path, PurePosixPath
from datetime import datetime
import hashlib
import io
import json
import re
import shutil
import tarfile

root = Path(__file__).resolve().parents[3]
work = Path(__file__).resolve().parent / "integration_05"
if (work / "build_exit.txt").read_text().strip() != "0" or \
        "ARM64_INTEGRATION_BUILD_AND_PACKAGE_PASSED=1" not in (work / "logs/build.log").read_text(encoding="utf-8"):
    raise RuntimeError("完整编译/依赖/打包没有完成，未归档为发布输入。")

def sha(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()

package_name = (work / "packages/latest_package").read_text(encoding="utf-8").strip()
if package_name.startswith("/mnt/c/"):
    package_name = "C:/" + package_name[7:]
package = Path(package_name)
release = re.fullmatch(r"sn100_shell_([a-f0-9]{64})\.tar\.gz", package.name)
if not release:
    raise RuntimeError("发布包名称或指纹格式无效。")
release_id = release.group(1)
snapshot = json.loads((work / "source_snapshot.json").read_text(encoding="utf-8"))
program = root / "06_programs/sn100_host"
destination = root / "07_backups/sn100_host/board_b/workflow_integration_20261102/integration_05"
destination.mkdir(parents=True, exist_ok=False)
source_archive = destination / ("sn100_sources_" + release_id + ".tar.gz")
with tarfile.open(source_archive, "w:gz") as bundle:
    for name, expected in sorted(snapshot.items()):
        relative = PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise RuntimeError("源码指纹清单中出现越界路径。")
        content = (program / name).read_bytes()
        if hashlib.sha256(content).hexdigest() != expected:
            raise RuntimeError("源码已变化，未生成混合版本归档：" + name)
        item = tarfile.TarInfo("06_programs/sn100_host/" + relative.as_posix())
        item.size = len(content)
        item.mode = 0o644
        bundle.addfile(item, io.BytesIO(content))
target_package = destination / package.name
shutil.copyfile(package, target_package)
if sha(package) != sha(target_package):
    raise RuntimeError("程序包复制后指纹不符。")
shutil.copyfile(work / "source_snapshot.json", destination / "source_snapshot.json")
shutil.copyfile(work / "dependencies/dependency_report.json", destination / "dependency_report.json")
provenance = {
    "created_at": datetime.now().astimezone().isoformat(), "release_id": release_id,
    "intended_board": "board_b", "serial": "8314b1e8ffd0321f", "source_files": len(snapshot),
    "application_sha256": sha(work / "build/sn100_shell"),
    "report_renderer_sha256": sha(work / "build/sn100_report_renderer"),
    "package_sha256": sha(target_package), "source_archive_sha256": sha(source_archive),
    "build_and_dependencies": "passed", "board_activation": "not_performed",
    "rk_function": "pending_rk_validation", "instrument": "not_verified",
    "runtime_data_included": False,
}
(destination / "provenance.json").write_text(json.dumps(provenance, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
entries = [sha(path) + "  " + path.name for path in sorted(destination.iterdir()) if path.is_file()]
(destination / "SHA256SUMS").write_text("\n".join(entries) + "\n", encoding="ascii")
(work / "archive_path.txt").write_text(str(destination) + "\n", encoding="utf-8")
print(json.dumps(provenance, ensure_ascii=False))
print("ARCHIVE=" + str(destination))
