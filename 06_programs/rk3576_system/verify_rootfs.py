#!/usr/bin/env python3
"""检查生成系统中的配置和文件，不启动服务，也不代替RK板端验证。"""
from pathlib import Path
import configparser
import hashlib
import json
import os
import re
import stat
import struct
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
report = Path(sys.argv[2])
if root.name != "binary" or root.parent.name != "ubuntu":
    raise SystemExit("只检查本次SDK生成的ubuntu/binary目录。")
checks = []

def check(name, passed, actual):
    checks.append({"name": name, "passed": bool(passed), "actual": actual})

def ini(relative):
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    parser.read(root / relative)
    return parser

os_release = dict(line.split("=", 1) for line in (root / "etc/os-release").read_text().splitlines() if "=" in line)
check("ubuntu_version", os_release.get("ID", "").strip('"') == "ubuntu" and os_release.get("VERSION_ID", "").strip('"') == "24.04", os_release)

packages = {}
unfinished_packages = []
for paragraph in (root / "var/lib/dpkg/status").read_text().split("\n\n"):
    fields = dict(line.split(": ", 1) for line in paragraph.splitlines() if ": " in line and not line.startswith(" "))
    status = fields.get("Status", "")
    if status.endswith(" ok installed"):
        packages[fields["Package"]] = {"version": fields.get("Version"), "architecture": fields.get("Architecture")}
    elif status.startswith(("install ", "hold ")):
        unfinished_packages.append({"package": fields.get("Package"), "status": status})
check("package_configuration_complete", not unfinished_packages, unfinished_packages)
required = "samba openssh-server network-manager chrony rt-tests stress-ng iproute2 iputils-ping ethtool lsof procps gpiod".split()
for package in required:
    value = packages.get(package)
    check("package:" + package, value is not None and value["architecture"] in ("arm64", "all"), value)

account = next((line.split(":") for line in (root / "etc/passwd").read_text().splitlines() if line.startswith("lckfb:")), None)
check("home_registration", account and account[5] == "/home/rkshare/lckfb", account[5] if account else None)
uid = int(account[2]) if account else -1
for relative, mode in (("home/rkshare", 0o700), ("home/rkshare/lckfb", 0o750), ("home/rkshare/programs", 0o750)):
    path = root / relative
    value = path.stat() if path.exists() else None
    check("directory:" + relative, value and value.st_uid == uid and stat.S_IMODE(value.st_mode) == mode,
          {"uid": value.st_uid, "mode": oct(stat.S_IMODE(value.st_mode))} if value else None)
check("old_home_absent", not os.path.lexists(root / "home/lckfb"), os.path.lexists(root / "home/lckfb"))

samba = ini("etc/samba/smb.conf")
for name, expected in {"path": "/home/rkshare", "read only": "no", "guest ok": "no", "valid users": "lckfb", "create mask": "0600", "directory mask": "0700"}.items():
    actual = samba.get("rkshare", name, fallback=None)
    check("samba:" + name, actual == expected, actual)

network_path = root / "etc/NetworkManager/system-connections/rk3576-direct.nmconnection"
network = ini("etc/NetworkManager/system-connections/rk3576-direct.nmconnection")
for section, name, expected in (("connection", "interface-name", "end0"), ("connection", "autoconnect", "true"), ("ipv4", "method", "manual"), ("ipv4", "address1", "192.168.100.123/24"), ("ipv4", "never-default", "true")):
    actual = network.get(section, name, fallback=None)
    check("network:" + name, actual == expected, actual)
value = network_path.stat() if network_path.exists() else None
check("network_permissions", value and value.st_uid == 0 and stat.S_IMODE(value.st_mode) == 0o600,
      oct(stat.S_IMODE(value.st_mode)) if value else None)

services = ("ssh", "smbd", "nmbd", "NetworkManager", "chrony", "ssh-keygen-firstboot")
for service in services:
    link = root / f"etc/systemd/system/multi-user.target.wants/{service}.service"
    target = os.readlink(link) if link.is_symlink() else None
    mapped = (root / target.lstrip("/")) if target and target.startswith("/") else (link.parent / target) if target else None
    check("enabled:" + service, mapped and mapped.exists(), target)
unit = (root / "etc/systemd/system/ssh-keygen-firstboot.service").read_text()
check("ssh_key_generation", "ExecStart=/usr/bin/ssh-keygen -A" in unit, unit)
key_dependency = root / "etc/systemd/system/ssh.service.requires/ssh-keygen-firstboot.service"
check("ssh_key_dependency", key_dependency.is_symlink(), os.readlink(key_dependency) if key_dependency.is_symlink() else None)
check("ssh_keys_absent", not list((root / "etc/ssh").glob("ssh_host_*_key")), "filenames_only")
for relative in ("var/lib/samba/private/passdb.tdb", "var/lib/samba/private/secrets.tdb"):
    check("credentials_absent:" + relative, not (root / relative).exists(), (root / relative).exists())
check("machine_id_empty", (root / "etc/machine-id").stat().st_size == 0, (root / "etc/machine-id").stat().st_size)
version = "6.1.99-rt36-tspi"
module_dir = root / "lib/modules" / version
check("rt_modules", module_dir.is_dir() and any(module_dir.rglob("*.ko")), version)
baseline = root.parent.parent / "output/kernel-modules/lib/modules" / version
sdk = root.parent.parent
strip_tool = sdk / "prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-strip"
def stripped_hashes(paths):
    result = {}
    with tempfile.TemporaryDirectory(prefix="rk3576-module-check-") as directory:
        for index, (name, path) in enumerate(paths):
            output = Path(directory) / str(index)
            subprocess.run([str(strip_tool), "--strip-unneeded", "-o", str(output), str(path)], check=True, capture_output=True)
            result[name] = hashlib.sha256(output.read_bytes()).hexdigest()
    return result
# 官方91-modules会去掉模块调试信息，使用同一工具处理临时副本后核对，原编译文件保持原样。
expected_modules = stripped_hashes((str(path.relative_to(baseline)), path) for path in baseline.rglob("*.ko"))
image_modules = {str(path.relative_to(module_dir)): hashlib.sha256(path.read_bytes()).hexdigest() for path in module_dir.rglob("*.ko")}
module_differences = sorted(name for name in expected_modules if expected_modules.get(name) != image_modules.get(name))
check("rt_module_hashes", bool(expected_modules) and not module_differences,
      {"compiled_count": len(expected_modules), "image_count": len(image_modules), "differences": module_differences,
       "comparison": "official toolchain strip --strip-unneeded on temporary copies",
       "additional_drivers": sorted(image_modules.keys() - expected_modules.keys())})
wifi_expected = stripped_hashes((name, sdk / "external/rkwifibt/drivers/bcmdhd" / name) for name in ("bcmdhd.ko", "bcmdhd_pcie.ko"))
wifi_differences = [name for name, expected in wifi_expected.items() if not (module_dir.parent / name).is_file()
                    or hashlib.sha256((module_dir.parent / name).read_bytes()).hexdigest() != expected]
check("external_wifi_module_hashes", not wifi_differences, {"count": len(wifi_expected), "differences": wifi_differences})
wrong_modules = []
vermagics = set()
for path in module_dir.parent.rglob("*.ko"):
    data = path.read_bytes()
    value = re.search(rb"vermagic=([^\x00]+)", data)
    vermagic = value.group(1).decode("ascii") if value else ""
    vermagics.add(vermagic)
    if not (len(data) >= 20 and data[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<H", data, 18)[0] == 183
            and vermagic.startswith(version + " ") and "preempt_rt" in vermagic):
        wrong_modules.append(str(path.relative_to(module_dir.parent)))
check("all_modules_match_rt_kernel", bool(image_modules) and not wrong_modules,
      {"vermagics": sorted(vermagics), "wrong_modules": wrong_modules})
for relative in ("usr/sbin/sshd", "usr/sbin/smbd", "usr/bin/cyclictest"):
    path = root / relative
    header = path.read_bytes()[:20] if path.exists() else b""
    check("arm64:" + relative, header[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<H", header, 18)[0] == 183 if len(header) >= 20 else False, header.hex())
helper = root / "usr/local/sbin/rk3576-first-setup"
check("first_setup_helper", helper.is_file() and bool(helper.stat().st_mode & stat.S_IXUSR),
      hashlib.sha256(helper.read_bytes()).hexdigest() if helper.is_file() else None)
result = {"scope": "只检查生成系统的文件与配置；板子不在身边，未启动服务，未验收硬件", "passed": sum(c["passed"] for c in checks), "failed": sum(not c["passed"] for c in checks), "checks": checks}
report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
print(f"ROOTFS_CONTENT_CHECKS={result['passed']} FAILED={result['failed']}")
raise SystemExit(0 if result["failed"] == 0 else 1)
