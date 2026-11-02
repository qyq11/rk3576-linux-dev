#!/usr/bin/env python3
"""用电脑上的只读文件系统工具，核对实际根文件系统镜像内的关键文件。"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tempfile

if os.geteuid() != 0:
    raise SystemExit("请通过sudo运行，以只读方式核对系统中仅root可读的网络配置。")
sdk = Path(sys.argv[1]).resolve()
report = Path(sys.argv[2])
root = sdk / "ubuntu/binary"
image = sdk / "ubuntu/ubuntu-rootfs.img"
assert root.is_dir() and image.is_file()
files = [
    "etc/os-release", "etc/passwd", "etc/machine-id", "etc/samba/smb.conf",
    "etc/NetworkManager/system-connections/rk3576-direct.nmconnection",
    "etc/netplan/90-rk3576-image.yaml",
    "etc/systemd/system/ssh-keygen-firstboot.service",
    "usr/local/sbin/rk3576-first-setup", "usr/share/rk3576-image/environment.txt",
    "usr/share/rk3576-image/installed-packages.tsv",
    "usr/sbin/sshd", "usr/sbin/smbd", "usr/bin/cyclictest",
]
files += [str(path.relative_to(root)) for path in sorted((root / "lib/modules").rglob("*.ko"))]
checks = []
with tempfile.TemporaryDirectory(prefix="rk3576-image-check-") as directory:
    for index, relative in enumerate(files):
        output = Path(directory) / str(index)
        physical = (root / relative).resolve().relative_to(root)
        # debugfs不使用-w参数，整个检查只读取镜像；dump文件写入电脑临时目录。
        result = subprocess.run(["debugfs", "-R", f"dump /{physical} {output}", str(image)], capture_output=True, text=True)
        expected = hashlib.sha256((root / relative).read_bytes()).hexdigest()
        actual = hashlib.sha256(output.read_bytes()).hexdigest() if output.is_file() else None
        checks.append({"path": relative, "passed": result.returncode == 0 and actual == expected, "sha256": actual})
result = {"scope": "只读核对实际根文件系统镜像中的关键文件和全部实时内核模块；未启动RK系统", "passed": sum(c["passed"] for c in checks), "failed": sum(not c["passed"] for c in checks), "checks": checks}
report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
print(f"PACKED_IMAGE_FILE_CHECKS={result['passed']} FAILED={result['failed']}")
raise SystemExit(0 if result["failed"] == 0 else 1)
