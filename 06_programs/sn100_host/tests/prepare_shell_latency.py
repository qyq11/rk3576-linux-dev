#!/usr/bin/env python3
"""Freeze the public shell-latency dependency set for repeatable evidence.

This helper never opens a runtime DB, tag, secret, reference tree or user config.
Once created, a snapshot is verified and reused; live O03-3 edits cannot silently
change the code between repeated latency runs. A failed partial freeze requires
inspection instead of being overwritten automatically.
"""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys


PUBLIC_SUFFIXES = {
    ".c", ".cpp", ".h", ".hpp", ".inc", ".ui", ".pro", ".pri", ".qrc",
    ".png", ".svg", ".ico", ".jpg", ".jpeg", ".gif", ".json", ".xml",
    ".qss", ".ts", ".qm", ".ttf", ".otf", ".rpt", ".js", ".qml",
    ".sh", ".py", ".md", ".txt",
}
SKIP_DIRS = {
    ".git", ".moc", ".obj", ".rcc", ".ui", "__pycache__", "build",
    "bin", "out", "debug", "release", "runtime", "logs", "temp",
    "source_snapshot", "generated", "node_modules",
}
SENSITIVE_PART = re.compile(r"(?:^|[_\-.])(secret|password|credential|private|bootstrap)(?:[_\-.]|$)", re.I)
MARKER = b"class ShellAccountTest : public QObject"
# Match this test target's actual qmake dependency boundary. Other chats can
# edit protocol/quality/coagulation/report rendering without invalidating this
# shell/account/hospital diagnosis. Shared shell/core changes still abort races.
COPY_TREES = ("shell", "data", "ui", "resources")
COPY_FILES = (
    "AGENTS.md",
    "history/history_core.pri",
    "history/history_store.cpp", "history/history_store.h",
    "history/history_page.cpp", "history/history_page.h",
    "history/result_statistics.cpp", "history/result_statistics.h",
    "history/frm_report_material.ui", "history/mask_dlg_query_report.ui",
    "history/mask_dlg_report_detail.ui", "history/mask_dlg_export.ui",
    "history/resources.qrc",
    "tests/shell_account_test.cpp",
    "tests/shell_latency_test.cpp", "tests/shell_latency_test.pro",
    "tests/shell_latency.sh", "tests/prepare_shell_latency.py",
    "tests/fixtures/history_reports.json",
)


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def write_json(path, value):
    temporary = path.with_name(path.name + ".new")
    with temporary.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    os.replace(temporary, path)


def require_contained(path, root, allow_root=False):
    resolved = path.resolve()
    if resolved != root and root not in resolved.parents:
        raise RuntimeError("Path is outside the dedicated latency work directory")
    if not allow_root and resolved == root:
        raise RuntimeError("An independent child path is required")
    return resolved


def validate_work(program, work_text):
    """Allow the original root or exactly one explicitly named child attempt.

    Validate before creating ANY part of an environment-selected directory.
    Failed attempts are never moved, cleared or reused as corrected snapshots.
    """
    base = (program.parent.parent / "temp" / "sn100_host" / "o03_2_latency").resolve()
    path = Path(work_text)
    if not path.is_absolute() or str(path) != work_text or any(part in {".", ".."} for part in work_text.split("/")):
        raise RuntimeError("Latency work must be a canonical absolute path without dot components")
    if path != base and (path.parent != base / "attempts" or not re.fullmatch(r"[a-z0-9_-]+", path.name)):
        raise RuntimeError("Latency work must be the default root or exactly attempts/<simple-id> beneath it")
    for candidate in [path, *path.parents]:
        if candidate.is_symlink():
            raise RuntimeError("Latency work cannot traverse a symlink")
    if path.resolve() != path:
        raise RuntimeError("Latency work is not canonical")
    return path


def public_sources(program):
    result = []
    for relative in COPY_FILES:
        candidate = program / relative
        if candidate.is_symlink() or not candidate.is_file() or any(parent.is_symlink() for parent in candidate.parents):
            raise RuntimeError("Required regular dependency is missing or symlinked: " + relative)
        result.append(candidate)
    for relative in COPY_TREES:
        source_tree = program / relative
        if source_tree.is_symlink() or not source_tree.is_dir():
            raise RuntimeError("Required regular source tree is missing: " + relative)
        for current, directories, names in os.walk(source_tree, followlinks=False):
            parent = Path(current)
            kept = []
            for name in sorted(directories):
                candidate = parent / name
                if name.lower() in SKIP_DIRS or name.lower().startswith("build-"):
                    continue
                if candidate.is_symlink():
                    raise RuntimeError("Source symlink directory is not copied: " + str(candidate.relative_to(program)))
                kept.append(name)
            directories[:] = kept
            for name in sorted(names):
                candidate = parent / name
                if candidate.suffix.lower() not in PUBLIC_SUFFIXES and name not in {"LICENSE", "NOTICE", "COPYING"}:
                    continue
                if SENSITIVE_PART.search(name) or name.startswith(("moc_", "ui_", "qrc_")):
                    continue
                if candidate.is_symlink() or not candidate.is_file():
                    raise RuntimeError("Only regular public source files may be frozen")
                result.append(candidate)
    return sorted(result, key=lambda item: item.relative_to(program).as_posix())


def verify_snapshot(work):
    manifest_path = work / "source_manifest.json"
    if not manifest_path.is_file():
        raise RuntimeError("No completed source manifest; inspect any partial snapshot before retry")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    snapshot = work / "source_snapshot"
    for entry in manifest["files"]:
        path = require_contained(snapshot / entry["path"], snapshot.resolve())
        if path.is_symlink() or not path.is_file() or path.stat().st_size != entry["bytes"] or digest(path) != entry["sha256"]:
            raise RuntimeError("Frozen source fingerprint mismatch: " + entry["path"])
    generated = work / "generated" / "latency_support.h"
    if not generated.is_file() or digest(generated) != manifest["generated_support_sha256"]:
        raise RuntimeError("Generated latency support fingerprint mismatch")
    return manifest


def prepare(program, work):
    snapshot = work / "source_snapshot"
    if snapshot.exists():
        manifest = verify_snapshot(work)
        print("Reusing verified frozen source: %d files" % len(manifest["files"]))
        return
    needed = [*COPY_FILES, "shell/shell.pri"]
    for relative in needed:
        if not (program / relative).is_file():
            raise RuntimeError("Required source is not ready: " + relative)
    before_paths = public_sources(program)
    entries = [{"path": path.relative_to(program).as_posix(), "bytes": path.stat().st_size,
                "sha256": digest(path)} for path in before_paths]
    # Persist the pre-copy identities before copying anything. No credentials
    # are in this allowlisted set; race detection compares hashes after copy.
    pre_copy = {"created_utc": utc_now(), "source_root": str(program), "files": entries}
    write_json(work / "source_manifest.pre_copy.json", pre_copy)
    snapshot.mkdir()
    for entry in entries:
        source = program / entry["path"]
        destination = snapshot / entry["path"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        # Copy bytes only: never follow an unexpectedly replaced source link.
        if source.is_symlink():
            raise RuntimeError("Source became a symlink during freeze")
        payload = source.read_bytes()
        if hashlib.sha256(payload).hexdigest() != entry["sha256"]:
            raise RuntimeError("Source changed before copy: " + entry["path"])
        with destination.open("xb") as output:
            output.write(payload)
        if digest(destination) != entry["sha256"]:
            raise RuntimeError("Copy checksum mismatch: " + entry["path"])
    after_paths = public_sources(program)
    if [path.relative_to(program).as_posix() for path in after_paths] != [entry["path"] for entry in entries]:
        raise RuntimeError("Public source file set changed during freeze")
    for entry in entries:
        source = program / entry["path"]
        if source.is_symlink() or source.stat().st_size != entry["bytes"] or digest(source) != entry["sha256"]:
            raise RuntimeError("Live source changed during freeze: " + entry["path"])
    account_test = (snapshot / "tests" / "shell_account_test.cpp").read_bytes()
    if account_test.count(MARKER) != 1:
        raise RuntimeError("Cannot identify the original account-test helper boundary")
    prefix = account_test.split(MARKER, 1)[0]
    if b"Q_OBJECT" in prefix or b"shell_account_test.moc" in prefix:
        raise RuntimeError("Generated helper prefix unexpectedly includes QObject test/moc code")
    generated = work / "generated"
    generated.mkdir()
    support = generated / "latency_support.h"
    support.write_bytes(b"// Generated from the SHA-verified frozen O03-2 test helper prefix.\n#pragma once\n" + prefix)
    manifest = {**pre_copy, "snapshot_root": str(snapshot), "freeze_completed_utc": utc_now(),
                "purpose": "Public-source-only frozen WSL latency diagnosis; preserves original layout/animation",
                "copy_scope": {"public_trees": COPY_TREES, "explicit_dependencies": COPY_FILES,
                               "excluded_parallel_modules": ["protocol", "quality", "coagulation", "thirdparty", "unrelated history reporting/tests/docs"]},
                "generated_support_sha256": digest(support),
                "generated_from": "tests/shell_account_test.cpp prefix before class ShellAccountTest : public QObject",
                "runtime_exclusion": "No DB, SQLite sidecars, tag, secret, user configuration, reference or temp tree is copied"}
    write_json(work / "source_manifest.json", manifest)
    write_json(work / "prepare_metadata.json", {"created_utc": utc_now(), "python": sys.version,
               "platform": platform.platform(), "snapshot_files": len(entries),
               "source_manifest_sha256": digest(work / "source_manifest.json")})
    verify_snapshot(work)
    print("Frozen and verified %d public source/resource files" % len(entries))


def command_version(command):
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT).strip()


def mark_built(work, binary):
    manifest = verify_snapshot(work)
    binary = require_contained(binary, work)
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise RuntimeError("Successful fresh executable is required")
    write_json(work / "build_metadata.json", {"status": "success", "completed_utc": utc_now(),
        "binary": str(binary), "binary_sha256": digest(binary), "binary_bytes": binary.stat().st_size,
        "source_manifest_sha256": digest(work / "source_manifest.json"),
        "generated_support_sha256": manifest["generated_support_sha256"],
        "qmake": command_version(["/usr/lib/qt5/bin/qmake", "-v"]),
        "compiler": command_version(["g++", "--version"]), "platform": platform.platform(),
        "configuration": "Qt5 Widgets/SQL/Testlib; C++14; Release; linux-g++; frozen source",
        "gui_execution": "Not performed by build; requires a separately coordinated run slot"})
    print("Recorded successful fresh build and binary SHA-256")


def verify_built(work):
    verify_snapshot(work)
    metadata_path = work / "build_metadata.json"
    if not metadata_path.is_file():
        raise RuntimeError("No successful build metadata; refusing to run any old executable")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    binary = require_contained(Path(metadata["binary"]), work)
    if metadata.get("status") != "success" or not binary.is_file() or not os.access(binary, os.X_OK):
        raise RuntimeError("No verified successful executable")
    if digest(binary) != metadata["binary_sha256"] or digest(work / "source_manifest.json") != metadata["source_manifest_sha256"]:
        raise RuntimeError("Build/source fingerprint mismatch; refusing stale or changed target")
    return metadata


def prepare_run(work, evidence):
    metadata = verify_built(work)
    if not evidence.is_absolute():
        raise RuntimeError("SN100_LATENCY_EVIDENCE_DIR must be absolute")
    resolved = require_contained(evidence, work)
    if evidence != resolved or evidence.exists():
        raise RuntimeError("Evidence must be a new canonical absolute child directory of the latency work root")
    if resolved.relative_to(work).parts[0] in {"source_snapshot", "generated", "build", "logs", ".build_lock"}:
        raise RuntimeError("Evidence cannot be created inside frozen sources, generated headers, build outputs or logs")
    # Reject symlinked ancestors before mkdir; no old O02/O03 data can be reused.
    for parent in [evidence, *evidence.parents]:
        if parent == work:
            break
        if parent.is_symlink():
            raise RuntimeError("Evidence path cannot traverse a symlink")
    evidence.mkdir(parents=True, exist_ok=False)
    write_json(evidence / "run_buildmetadata.json", {**metadata, "run_started_utc": utc_now(),
        "evidence_root": str(evidence), "qpa_platform": "xcb", "runtime_data": "New anonymous test-owned data only"})
    print("Verified frozen build; prepared new independent evidence directory")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["prepare", "verify", "mark-built", "verify-built", "prepare-run"])
    parser.add_argument("--program-dir", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--work-dir", help="Canonical default root or its attempts/<simple-id>; otherwise SN100_LATENCY_WORK_DIR")
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--evidence-dir", type=Path)
    arguments = parser.parse_args()
    program = arguments.program_dir.resolve()
    default_work = str((program.parent.parent / "temp" / "sn100_host" / "o03_2_latency").resolve())
    if not (program / "AGENTS.md").is_file():
        raise RuntimeError("Use this repository's formal sn100_host source and dedicated latency work root")
    work = validate_work(program, arguments.work_dir or os.environ.get("SN100_LATENCY_WORK_DIR") or default_work)
    work.mkdir(parents=True, exist_ok=True)
    if arguments.mode == "prepare":
        prepare(program, work)
    elif arguments.mode == "verify":
        verify_snapshot(work)
        print("Frozen source verified")
    elif arguments.mode == "mark-built":
        if arguments.binary is None:
            parser.error("mark-built requires --binary")
        mark_built(work, arguments.binary)
    elif arguments.mode == "verify-built":
        verify_built(work)
        print("Frozen source and successful executable verified")
    else:
        if arguments.evidence_dir is None:
            parser.error("prepare-run requires --evidence-dir")
        prepare_run(work, arguments.evidence_dir)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        print("Latency preparation refused: " + str(error), file=sys.stderr)
        sys.exit(2)
