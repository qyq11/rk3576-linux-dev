#!/usr/bin/env python3
"""Host-only source fingerprint and immutable offline RK release packaging."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tarfile
from datetime import datetime, timezone


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(program):
    paths = []
    for name in ("shell", "data", "ui", "history", "resources", "thirdparty/qtrpt"):
        paths.extend(p for p in (program / name).rglob("*")
                     if p.is_file() and p.suffix not in (".md", ".pyc")
                     and "__pycache__" not in p.parts)
    paths.extend(program / name for name in ("prepare_arm64.sh", "resources.qrc",
                 "tests/fixtures/accounts.json", "tests/fixtures/history_reports.json",
                 "tests/fixtures/history_report_options.json"))
    return {str(p.relative_to(program)): sha(p) for p in sorted(set(paths))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("snapshot", "package"))
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--sysroot", type=Path)
    parser.add_argument("--snapshot", type=Path)
    parser.add_argument("--dependencies", type=Path)
    args = parser.parse_args()
    program = Path(__file__).resolve().parent.parent
    if not args.output.is_absolute():
        parser.error("--output must be absolute")
    current = snapshot(program)
    if args.action == "snapshot":
        args.output.write_text(json.dumps(current, indent=2) + "\n", encoding="utf-8")
        return
    if not args.binary or not args.sysroot or not args.snapshot or not args.dependencies:
        parser.error("package requires --binary, --sysroot, --snapshot and --dependencies")
    if json.loads(args.snapshot.read_text(encoding="utf-8")) != current:
        raise RuntimeError("Source inputs changed during build; rebuild before publishing")
    args.output.mkdir(parents=True, exist_ok=True)
    stage = args.output / ("stage_" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S_%f"))
    stage.mkdir(mode=0o700)
    renderer = args.binary.parent / "sn100_report_renderer"
    report = json.loads(args.dependencies.read_text(encoding="utf-8"))
    if report.get("errors") or not report.get("offline_link_and_sql_closure_passed"):
        raise RuntimeError("A successful matching dependency report is required")
    for binary, role in ((args.binary, "main"), (renderer, "report_renderer")):
        records = [item for item in report.get("files", []) if role in item["roles"]]
        if len(records) != 1 or records[0]["sha256"] != sha(binary):
            raise RuntimeError("Executable differs from the checked closure: " + str(binary))
    shutil.copy2(args.binary, stage / "sn100_shell")
    shutil.copy2(renderer, stage / "sn100_report_renderer")
    shutil.copy2(program / "shell/rk_board.sh", stage / "rk_board.sh")
    (stage / "sn100_shell").chmod(0o700)
    (stage / "sn100_report_renderer").chmod(0o700)
    sysroot = args.sysroot.resolve()
    private = {
        "usr/lib/aarch64-linux-gnu/libQt5Sql.so.5": "lib/libQt5Sql.so.5",
        "usr/lib/aarch64-linux-gnu/libsqlite3.so.0": "lib/libsqlite3.so.0",
        "usr/lib/aarch64-linux-gnu/qt5/plugins/sqldrivers/libqsqlite.so": "plugins/sqldrivers/libqsqlite.so",
        "usr/lib/aarch64-linux-gnu/qt5/plugins/platforms/libqoffscreen.so": "plugins/platforms/libqoffscreen.so",
    }
    candidates = set((sysroot / "report_private_libraries.txt").read_text(encoding="utf-8").splitlines())
    for record in report["files"]:
        target_path = record.get("target_path")
        if target_path in candidates and record.get("soname"):
            private[target_path.lstrip("/")] = "lib/" + record["soname"]
    for source, dest in private.items():
        resolved = (sysroot / source).resolve(strict=True)
        if not resolved.is_relative_to(sysroot):
            raise RuntimeError("Runtime input escapes isolated sysroot: " + source)
        target = stage / dest
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(resolved, target)
    fonts = {}
    for name in ("NotoSansCJK-Regular.ttc", "NotoSansCJK-Bold.ttc"):
        source = sysroot / "usr/share/fonts/opentype/noto" / name
        target = stage / "fonts" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        fonts[name] = {"sha256": sha(source), "source": "archived fonts-noto-cjk 1:20230817+repack1-3"}
    shutil.copy2(sysroot / "usr/share/doc/fonts-noto-cjk/copyright", stage / "fonts/LICENSE.txt")
    (stage / "licenses").mkdir(exist_ok=True)
    shutil.copy2(sysroot / "usr/share/doc/libqt5gui5t64/copyright", stage / "licenses/QtBaseCopyright.txt")
    for package in ("libfontconfig1", "libexpat1"):
        shutil.copy2(sysroot / "usr/share/doc" / package / "copyright", stage / "licenses" / (package + ".txt"))
    (stage / "fonts/fonts.conf").write_text(
        '<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">\n'
        '<fontconfig><include ignore_missing="yes">/etc/fonts/fonts.conf</include>'
        '<dir prefix="relative">.</dir><cachedir prefix="xdg">fontconfig</cachedir></fontconfig>\n', encoding="utf-8")
    info = {"schema": "sn100-offline-rk-release-v1", "binary_sha256": sha(args.binary),
            "renderer_sha256": sha(renderer), "capabilities": ["report_preview_pdf"],
            "report_template": {"path": ":/history_report/report.xml", "sha256": sha(program / "history/templates/report.xml"), "embedded": True},
            "fonts": fonts, "dependencies_sha256": sha(args.dependencies),
            "architecture": "aarch64", "mode": "offline", "pages": ["hospital", "account", "history_csv", "report_preview_pdf"],
            "private_runtime": private, "sources": current, "board_runtime_verified": False,
            "system_runtime": "Existing Qt5.15.13 Core/Gui/Widgets, EGLFS/KMS and GPU/input plugins; preflight required"}
    (stage / "release.json").write_text(json.dumps(info, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    lines = [f"{sha(p)}  {p.relative_to(stage).as_posix()}\n"
             for p in sorted(stage.rglob("*")) if p.is_file()]
    (stage / "SHA256SUMS").write_text("".join(lines), encoding="ascii")
    release_id = sha(stage / "SHA256SUMS")
    (stage / "release_id").write_text(release_id + "\n", encoding="ascii")
    archive = args.output / ("sn100_shell_" + release_id + ".tar.gz")
    temporary = archive.with_suffix(".gz.new")
    with tarfile.open(temporary, "w:gz", dereference=True) as bundle:
        for path in sorted(stage.rglob("*")):
            bundle.add(path, arcname=path.relative_to(stage).as_posix(), recursive=False)
    temporary.replace(archive)
    (args.output / "latest_package").write_text(str(archive) + "\n", encoding="utf-8")
    (args.output / "latest_release").write_text(str(stage) + "\n", encoding="utf-8")
    archive.with_suffix(".gz.sha256").write_text(f"{sha(archive)}  {archive.name}\n", encoding="ascii")
    print("RELEASE_ID=" + release_id)
    print("PACKAGE=" + str(archive))
    print("PACKAGE_SHA256=" + sha(archive))


if __name__ == "__main__":
    main()
