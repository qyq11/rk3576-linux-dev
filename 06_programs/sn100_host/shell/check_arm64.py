#!/usr/bin/env python3
"""Inspect ARM64 inputs on the host; never execute a target ELF or contact RK.

An offline pass covers the main ELF, SQLite plugin and archived display/input
plugins. Board drivers, plugin discovery, fonts and physical display remain a
separate acceptance step, even when every inspected dependency is available.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone


LIBRARY_DIRS = ("lib/aarch64-linux-gnu", "usr/lib/aarch64-linux-gnu", "lib", "usr/lib")
PLUGIN_ROOT = "usr/lib/aarch64-linux-gnu/qt5/plugins"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_readelf(output):
    """Read definitions separately from requirements; a high version is no proof
    that a provider defines every lower version requested by a consumer.
    """
    info = {"needed": [], "definitions": [], "requirements": {}, "search_paths": []}
    for field, key in (("Class", "class"), ("Data", "data"), ("Machine", "machine")):
        match = re.search(r"^\s*" + field + r":\s*(.+)$", output, re.MULTILINE)
        info[key] = match.group(1).strip() if match else ""
    match = re.search(r"Requesting program interpreter:\s*([^\]]+)\]", output)
    info["interpreter"] = match.group(1).strip() if match else None
    match = re.search(r"\(SONAME\).*\[([^\]]+)\]", output)
    info["soname"] = match.group(1) if match else None
    info["needed"] = re.findall(r"\(NEEDED\).*\[([^\]]+)\]", output)
    for tag, paths in re.findall(r"\((RPATH|RUNPATH)\).*\[([^\]]*)\]", output):
        info["search_paths"].append({"tag": tag, "paths": paths.split(":")})
    section = None
    provider = None
    for line in output.splitlines():
        if line.startswith("Version definition section"):
            section = "definitions"
        elif line.startswith("Version needs section"):
            section = "requirements"
        elif line.startswith("Version symbols section"):
            section = None
        if section == "definitions" and "Rev:" in line:
            match = re.search(r"\bName:\s*(\S+)", line)
            if match:
                info["definitions"].append(match.group(1))
        elif section == "requirements":
            match = re.search(r"\bFile:\s*(\S+)", line)
            if match:
                provider = match.group(1)
                info["requirements"].setdefault(provider, [])
            match = re.search(r"\bName:\s*(\S+)\s+Flags:\s*(.*?)\s+Version:", line)
            if match and provider:
                info["requirements"][provider].append({"name": match.group(1), "flags": match.group(2)})
    return info


class ReadelfReader:
    def __init__(self):
        self.command = shutil.which("readelf")
        if not self.command:
            raise ValueError("Host readelf is required (run this checker in WSL).")

    def __call__(self, path):
        result = subprocess.run(
            [self.command, "--wide", "-h", "-l", "-d", "--version-info", "--", str(path)],
            capture_output=True, text=True, timeout=30,
            env=dict(os.environ, LC_ALL="C"), check=False,
        )
        if result.returncode:
            raise ValueError("readelf rejected %s: %s" % (path, result.stderr.strip()[:1200]))
        return parse_readelf(result.stdout)


class Checker:
    def __init__(self, binary, sysroot, reader, additional_binaries=()):
        self.binary = Path(binary).resolve()
        self.sysroot = Path(sysroot).resolve()
        self.reader = reader
        self.additional_binaries = [Path(path).resolve() for path in additional_binaries]
        self.errors = []
        self.warnings = []
        self.files = {}
        self.edges = []
        self.version_checks = []
        self.runtime = {}

    def error(self, code, **details):
        item = {"code": code, **details}
        if item not in self.errors:
            self.errors.append(item)

    def target_file(self, target):
        """Interpret absolute symlinks inside the target root, never on the host."""
        parts = list(PurePosixPath(target).parts)
        if parts and parts[0] == "/":
            parts.pop(0)
        resolved = []
        links = 0
        while parts:
            part = parts.pop(0)
            if part in ("", "."):
                continue
            if part == "..":
                if not resolved:
                    raise ValueError("Target path escapes sysroot: " + target)
                resolved.pop()
                continue
            candidate = self.sysroot.joinpath(*resolved, part)
            if candidate.is_symlink():
                links += 1
                if links > 40:
                    raise ValueError("Target symlink loop: " + target)
                link = PurePosixPath(os.readlink(candidate))
                if link.is_absolute():
                    resolved = []
                    parts = list(link.parts[1:]) + parts
                else:
                    parts = list(link.parts) + parts
            else:
                resolved.append(part)
        result = self.sysroot.joinpath(*resolved)
        return result if result.is_file() else None

    def find_library(self, name, info=None, target=None):
        if "/" in name:
            if not name.startswith("/"):
                raise ValueError("Relative DT_NEEDED path is unsupported: " + name)
            candidates = [name]
        else:
            directories = []
            for entry in (info or {}).get("search_paths", []):
                for directory in entry["paths"]:
                    if "$ORIGIN" in directory or "${ORIGIN}" in directory:
                        if not target:
                            raise ValueError("Main ELF uses $ORIGIN; its target installation path is not supplied.")
                        origin = str(PurePosixPath(target).parent)
                        directory = directory.replace("${ORIGIN}", origin).replace("$ORIGIN", origin)
                    if not directory.startswith("/") or "$" in directory:
                        raise ValueError("Unsupported dynamic search path: " + directory)
                    directories.append(directory)
            candidates = [str(PurePosixPath(directory) / name) for directory in directories]
            candidates += ["/" + directory + "/" + name for directory in LIBRARY_DIRS]
        for candidate in candidates:
            result = self.target_file(candidate)
            if result:
                return result, "/" + result.relative_to(self.sysroot).as_posix()
        return None, None

    def inspect(self, path, role, target=None):
        path = Path(path)
        key = str(path)
        if key in self.files:
            if role not in self.files[key]["roles"]:
                self.files[key]["roles"].append(role)
            return self.files[key]
        before = sha256(path)
        info = self.reader(path)
        if before != sha256(path):
            self.error("input_changed_during_inspection", path=key)
        record = {"path": key, "target_path": target, "roles": [role], "sha256": before,
                  "bytes": path.stat().st_size, **info}
        self.files[key] = record  # Record before recursion: libc/loader have cycles.
        if info["class"] != "ELF64" or "little endian" not in info["data"] or info["machine"] != "AArch64":
            self.error("wrong_elf_architecture", path=key, elf_class=info["class"], machine=info["machine"], data=info["data"])
            return record
        for name in sorted(set(info["needed"]) | set(info["requirements"])):
            try:
                provider, provider_target = self.find_library(name, info, target)
            except ValueError as failure:
                self.error("unsupported_library_search", consumer=key, library=name, message=str(failure))
                continue
            self.edges.append({"consumer": key, "library": name, "provider": str(provider) if provider else None})
            if not provider:
                self.error("missing_dependency", consumer=key, library=name)
                continue
            provided = self.inspect(provider, "dependency", provider_target)
            if provided["soname"] and provided["soname"] != PurePosixPath(name).name:
                self.error("soname_mismatch", consumer=key, library=name, provider=str(provider), soname=provided["soname"])
            definitions = set(provided["definitions"])
            for requirement in info["requirements"].get(name, []):
                found = requirement["name"] in definitions
                self.version_checks.append({"consumer": key, "library": name, "provider": str(provider),
                                            "version": requirement["name"], "provided": found})
                if not found:
                    self.error("missing_symbol_version", consumer=key, library=name,
                               provider=str(provider), version=requirement["name"])
        return record

    def inspect_plugin_group(self, category, patterns):
        paths = set()
        for pattern in patterns:
            paths.update(self.sysroot.glob(PLUGIN_ROOT + "/" + pattern))
        checked = []
        for path in sorted(paths):
            target = "/" + path.relative_to(self.sysroot).as_posix()
            resolved = self.target_file(target)
            if not resolved:
                self.error("broken_plugin_path", path=str(path))
                continue
            self.inspect(resolved, "plugin:" + category, target)
            checked.append(str(resolved))
        self.runtime[category] = {"archived_files": checked,
                                  "board_verified": False,
                                  "status": "archived_static_check_only" if checked else "not_archived_board_check_required"}
        return checked

    def run(self):
        if not self.binary.is_file() or not self.sysroot.is_dir():
            raise ValueError("BINARY must be a file and --sysroot must be an existing directory.")
        main = self.inspect(self.binary, "main")
        for binary in self.additional_binaries:
            if not binary.is_file():
                self.error("missing_additional_binary", path=str(binary))
                continue
            helper = self.inspect(binary, "report_renderer")
            if helper["interpreter"] != "/lib/ld-linux-aarch64.so.1":
                self.error("unexpected_interpreter", path=str(binary), actual=helper["interpreter"], expected="/lib/ld-linux-aarch64.so.1")
        interpreter = main["interpreter"]
        if interpreter != "/lib/ld-linux-aarch64.so.1":
            self.error("unexpected_interpreter", actual=interpreter, expected="/lib/ld-linux-aarch64.so.1")
        if interpreter:
            loader = self.target_file(interpreter)
            if not loader:
                self.error("missing_interpreter", target_path=interpreter)
            else:
                self.inspect(loader, "interpreter", interpreter)
        sqlite_required = any("libQt5Sql.so.5" in item["needed"] for item in self.files.values())
        sqlite = self.inspect_plugin_group("sqlite", ["sqldrivers/libqsqlite.so"])
        if sqlite_required and not sqlite:
            self.error("missing_sqlite_plugin", target_path="/" + PLUGIN_ROOT + "/sqldrivers/libqsqlite.so")
        offscreen = self.inspect_plugin_group("report_offscreen", ["platforms/libqoffscreen.so"])
        if self.additional_binaries and not offscreen:
            self.error("missing_report_offscreen_plugin", target_path="/" + PLUGIN_ROOT + "/platforms/libqoffscreen.so")
        for category, patterns in (
            ("eglfs_platform", ["platforms/libqeglfs.so"]),
            ("egl_device_integration", ["egldeviceintegrations/*.so"]),
            ("input", ["generic/*evdev*.so", "generic/*libinput*.so"]),
            ("image_formats", ["imageformats/*.so"]),
        ):
            self.inspect_plugin_group(category, patterns)
        fonts = []
        for directory in ("usr/share/fonts", "usr/local/share/fonts", "share/fonts"):
            folder = self.sysroot / directory
            if folder.is_dir():
                fonts += [str(path.relative_to(self.sysroot)) for path in folder.rglob("*")
                          if path.is_file() and path.suffix.lower() in (".ttf", ".otf", ".ttc")]
        self.runtime["fonts"] = {"archived_files": sorted(fonts), "requested_family": "Noto Sans CJK SC",
                                 "board_verified": False, "family_and_glyph_coverage_verified": False}
        for record in self.files.values():
            path = Path(record["path"])
            if not path.is_file() or sha256(path) != record["sha256"]:
                self.error("input_changed_during_inspection", path=str(path))
        self.warnings += [
            "This is a static archive inspection, not execution or validation of the currently installed board libraries.",
            "EGLFS discovery, GPU/Mesa/GBM/DRM drivers, permissions, input devices and physical display need RK validation.",
            "Missing display/input plugins or fonts are board-runtime gaps; they do not make the main+SQLite closure pass false.",
            "PNG may be built into Qt; absent image-format plugins alone do not prove PNG support is missing.",
            "Qt plugin metadata/build compatibility and Noto Sans CJK SC glyphs must be verified on the target.",
            "Version definitions are checked per provider; this does not validate every relocated symbol or the dynamic linker's live search environment.",
        ]
        return {
            "schema": "sn100-arm64-offline-dependencies-v1", "created_utc": datetime.now(timezone.utc).isoformat(),
            "binary": str(self.binary), "sysroot": str(self.sysroot),
            "additional_binaries": [str(path) for path in self.additional_binaries],
            "status": "offline_dependency_check_failed" if self.errors else "offline_closure_passed_board_runtime_pending",
            "offline_link_and_sql_closure_passed": not self.errors, "board_runtime_verified": False,
            "sqlite_required": sqlite_required, "errors": self.errors, "warnings": self.warnings,
            "files": sorted(self.files.values(), key=lambda item: item["path"]),
            "dependency_edges": self.edges, "symbol_version_checks": self.version_checks,
            "runtime_components": self.runtime,
            "search_model": {"fixed_target_directories": ["/" + item for item in LIBRARY_DIRS],
                             "host_library_fallback": False, "target_elf_executed": False,
                             "network_or_board_access": False, "environment_LD_LIBRARY_PATH_used": False},
        }


def write_report(report, output):
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / "dependency_report.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = [report["status"], "Board execution/runtime acceptance: pending",
             "Inspected ELF files: %d; errors: %d" % (len(report.get("files", [])), len(report["errors"]))]
    lines += [json.dumps(error, ensure_ascii=False, sort_keys=True) for error in report["errors"]]
    summary = output / "dependency_summary.txt"
    summary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    checksums = {item["path"]: item["sha256"] for item in report.get("files", [])}
    checksums.update({str(path): sha256(path) for path in (report_path, summary)})
    (output / "SHA256SUMS").write_text("".join("%s  %s\n" % (digest, path) for path, digest in sorted(checksums.items())), encoding="utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--sysroot", required=True, type=Path)
    parser.add_argument("--additional-binary", action="append", default=[], type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args(argv)
    if not args.output_dir.is_absolute():
        parser.error("--output-dir must be absolute")
    output = args.output_dir.resolve()
    sysroot = args.sysroot.resolve()
    if output == sysroot or sysroot in output.parents or output == args.binary.resolve():
        parser.error("Reports must be outside the inspected sysroot and binary")
    try:
        report = Checker(args.binary, sysroot, ReadelfReader(), args.additional_binary).run()
    except (OSError, ValueError, subprocess.SubprocessError) as failure:
        report = {"schema": "sn100-arm64-offline-dependencies-v1", "status": "offline_dependency_check_failed",
                  "offline_link_and_sql_closure_passed": False, "board_runtime_verified": False,
                  "errors": [{"code": "inspection_error", "message": str(failure)}], "files": []}
    write_report(report, output)
    print(report["status"])
    print(str(output / "dependency_report.json"))
    return 1 if report["errors"] else 0


if __name__ == "__main__":
    sys.exit(main())
