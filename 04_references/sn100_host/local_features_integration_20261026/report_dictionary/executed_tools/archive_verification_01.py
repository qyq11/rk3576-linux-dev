#!/usr/bin/env python3
"""Archive completed formal dictionary metadata without executing a target."""
import hashlib
import json
from pathlib import Path
import re
import unicodedata


WORK = Path(__file__).resolve().parent
REPO = Path(__file__).resolve().parents[3]
SOURCE = WORK / "verification_01"
DEST = REPO / "04_references/sn100_host/local_features_integration_20261026/report_dictionary"
RECEIPT = WORK / "archive_verification_01.receipt.json"
MAX_TEXT_BYTES = 16 * 1024 * 1024
ALLOWED_TEXT = {".json", ".jsonl", ".log", ".txt", ".xml", ".py"}
ALLOWED_CONTROLS = {9, 10, 13}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def gate(data):
    if len(data) >= MAX_TEXT_BYTES:
        raise ValueError("Text member exceeds existing16MiB gate")
    if data.startswith((b"SQLite format 3", b"MZ", b"\x7fELF", b"PK\x03\x04")) or b"\x00" in data:
        raise ValueError("Binary/database/container signature or NUL")
    text = data.decode("utf-8", errors="strict")
    unexpected = sorted({ord(char) for char in text
                         if unicodedata.category(char) == "Cc" and ord(char) not in ALLOWED_CONTROLS})
    if unexpected:
        raise ValueError("Control codepoints outside TAB/LF/CR: " + str(unexpected))
    # Execution slot labels/authenticated booleans are metadata, not secrets.
    # Real key material cannot enter the text archive.
    if re.search(r"(?m)^-----BEGIN (?:[A-Z0-9]+ )?PRIVATE KEY-----\r?$", text):
        raise ValueError("Authentication key material")
    return {"strict_utf8": True, "nul_free": True, "allowed_controls_only": True,
            "binary_signature_free": True, "size_gate_passed": True, "byte_exceptions_used": []}


def main():
    if not (REPO / "06_programs/sn100_host").is_dir():
        raise SystemExit("Run the original temp archival tool, not its evidence snapshot")
    if DEST.exists():
        raise SystemExit("Preserve existing archive; no overwrite")
    verification = read_json(SOURCE / "verification.json")
    stage = read_json(SOURCE / "handoff.json")
    parent = read_json(SOURCE / "parent_exit_receipt.json")
    cleanup = read_json(SOURCE / "process_group_cleanup.json")
    receipt = read_json(SOURCE / "attempt_01/receipt.json")
    if sha((SOURCE / "handoff.json").read_bytes()) != "91ac682635194fb68c2de86709e6df340ddd2cbfd4d1c38234e25333b94af148":
        raise SystemExit("Completed verification handoff hash changed")
    assert verification["status"] == receipt["status"] == "passed"
    assert parent["actual_parent_exit_code"] == 0 and cleanup["remaining_count"] == 0
    assert stage["slot_released"] and verification["inputs_equal"] and verification["tools_equal"]
    assert receipt["source_count"] == 36 and len(receipt["tools_before"]) == 8
    assert len(receipt["steps"]) == 15 and all(step["exit_code"] == 0 and not step["timed_out"] for step in receipt["steps"])
    assert [run["actual"]["pass_incidents"] for target in receipt["targets"] for run in target["runs"]] == [10, 9, 9, 6, 6]

    selected, excluded, blocked = [], [], []
    for path in sorted(SOURCE.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(SOURCE)
        parts = relative.parts
        if path.is_symlink():
            raise SystemExit("Unreviewed symlink: " + str(path))
        if any(part == "runtime" for part in parts):
            reason = "runtime/database/cache forbidden"
        elif any(part in (".obj", ".moc", ".ui") for part in parts):
            reason = "generated build intermediate; its recorded hashes remain in raw receipt"
        elif path.name == "Makefile":
            reason = None
        elif path.suffix.lower() in ALLOWED_TEXT:
            reason = None
        elif path.suffix.lower() in (".png", ".jpg", ".jpeg", ".webp"):
            data = path.read_bytes()
            blocked.append({"source": path.relative_to(REPO).as_posix(), "bytes": len(data), "sha256": sha(data),
                            "reason": "No original anonymous screenshot was produced/approved by this test run"})
            continue
        else:
            reason = "object/executable/rebuildable cache or unexecuted wrapper excluded by allowlist"
        if reason:
            excluded.append({"source": path.relative_to(REPO).as_posix(), "bytes": path.stat().st_size, "reason": reason})
            continue
        data = path.read_bytes()
        try:
            checked = gate(data)
        except (UnicodeError, ValueError) as error:
            blocked.append({"source": path.relative_to(REPO).as_posix(), "bytes": len(data), "sha256": sha(data),
                            "reason": str(error)})
            continue
        destination = Path("verification_01") / relative
        if path.name == "Makefile":
            destination = destination.with_name("Makefile.txt")
        selected.append((path, destination, data, checked))
    own_data = Path(__file__).read_bytes()
    selected.append((Path(__file__), Path("executed_tools/archive_verification_01.py"), own_data, gate(own_data)))
    if blocked:
        RECEIPT.write_text(json.dumps({"status": "blocked-text-or-image-gate", "archive_created": False,
            "unapproved_members": blocked}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        raise SystemExit("Unknown non-UTF8/control/image member; see exact path/SHA/len in " + str(RECEIPT))

    DEST.mkdir(parents=True, exist_ok=False)
    expected, copied = {}, []

    def write(relative, data):
        checked = gate(data)
        destination = DEST / relative
        if not destination.resolve().is_relative_to(DEST.resolve()):
            raise ValueError("Archive target outside authorized root")
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("xb") as stream:
            stream.write(data)
        returned = destination.read_bytes()
        assert len(returned) == len(data) and sha(returned) == sha(data)
        gate(returned)
        expected[relative.as_posix()] = {"bytes": len(data), "sha256": sha(data), **checked}

    def save(relative, value):
        write(Path(relative), (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))

    for source, relative, data, checked in selected:
        assert source.read_bytes() == data
        write(relative, data)
        copied.append({"source": source.relative_to(REPO).as_posix(), "destination": relative.as_posix(),
                       "bytes": len(data), "sha256": sha(data), "readback_equal": True, **checked})
    save("excluded_members.json", {"schema": "sn100-dictionary-archive-exclusions-v1", "members": excluded,
         "note": "Forbidden runtime/DB/INI/object/exe and unnecessary generated intermediates were not archived; no source evidence removed."})
    save("archive_policy.json", {"schema": "sn100-dictionary-strict-text-archive-policy-v1",
         "strict_utf8": True, "max_text_bytes_exclusive": MAX_TEXT_BYTES, "allowed_control_codepoints": [9, 10, 13],
         "byte_exceptions_used": [], "content_conversion": False, "copied_raw_members": len(copied),
         "makefiles": "Three exact-byte Makefiles stored as Makefile.txt audit text; original source names are mapped.",
         "database_ini_runtime_object_exe_authentication_payloads_archived": False,
         "original_anonymous_images_produced": 0, "images_archived": 0,
         "source_delivery_manifest": "Raw delivery_text_files.json is unchanged source metadata, not the inclusion manifest for this narrower archive.",
         "warnings_failures": "All raw logs/XML copied byte-for-byte, including offscreen propagateSizeHints warnings. This attempt had no failed target or skipped case."})
    save("copy_readback.json", {"schema": "sn100-dictionary-raw-copy-readback-v1", "files": copied,
         "all_strict_text_gates_passed": True, "unknown_non_utf8": [], "exceptions_added": []})
    save("scope.json", {"schema": "sn100-dictionary-formal-archive-scope-v1", "scope": "Three fresh formal targets, five calls only",
         "actual_parent_exit": 0, "step_exit0_count": 15, "Qt_pass_fail_skip": [[10, 0, 0], [9, 0, 0], [9, 0, 0], [6, 0, 0], [6, 0, 0]],
         "new_formal_recursive_inputs": 36, "tools_before_after_unchanged": 8, "fresh_objects_not_archived": 20,
         "recorded_pgids_gone": 15, "warnings": {"offscreen_selector": 7, "offscreen_page": 20, "xcb": 0,
          "message": "This plugin does not support propagateSizeHints()", "raw_XML_preserved": True},
         "no_failed_attempt_hidden": True, "attempt_count": 1, "assertions_timeouts_or_formal_source_changed_for_run": False,
         "limits": ["Real HospitalStore SQL and real HistoryStore patient save were tested separately; page dictionary replies were controlled inputs.",
                    "Real HospitalStore-to-page unified shell route still awaits root integration/runtime proof.",
                    "No original Windows visual/physical input, Windows32/ARM64 build, RK/instrument, accepted resource or long-term validation is implied.",
                    "No legacy447 pass is inherited by this new formal scope."], "master_README_plan_record_owner": "root"})
    save("file_manifest.json", {"schema": "sn100-dictionary-archive-file-manifest-v1", "members": expected.copy(),
         "self_and_final_handoff": "Not self-referenced; all files including these two are re-read against in-memory expected bytes and reported in external archive receipt."})
    save("handoff.json", {"schema": "sn100-report-dictionary-archived-formal-verification-v1",
         "status": "archived-with-strict-text-gates-and-length-sha-readback", "source": SOURCE.relative_to(REPO).as_posix(),
         "archive_root": DEST.relative_to(REPO).as_posix(), "source_handoff_sha256": "91ac682635194fb68c2de86709e6df340ddd2cbfd4d1c38234e25333b94af148",
         "raw_verification_handoff": "verification_01/handoff.json", "actual_parent_exit": 0,
         "Qt_pass_totals": [10, 9, 9, 6, 6], "failed_or_skipped": 0, "runtime_receipt": "verification_01/attempt_01/receipt.json",
         "input_manifest": "verification_01/attempt_01/formal_inputs.json", "case_receipts": "verification_01/case_receipts.json",
         "cleanup": "verification_01/process_group_cleanup.json", "scope": "scope.json", "copy_readback": "copy_readback.json",
         "file_manifest": "file_manifest.json", "excluded_members": "excluded_members.json", "text_gate_policy": "archive_policy.json",
         "raw_files_copied": len(copied), "images_archived": 0, "unknown_non_utf8": [], "byte_exceptions_added": [],
         "no_build_gui_or_target_rerun_during_archive": True, "formal_source_or_old_execution_changes": False,
         "unified_HospitalStore_to_page_shell": "Not exercised by these targets; root integration remains pending.",
         "master_README_plan_02_owner": "root", "file_manifest_sha256": expected["file_manifest.json"]["sha256"]})
    final = []
    for relative, wanted in sorted(expected.items()):
        data = (DEST / relative).read_bytes()
        checked = gate(data)
        assert len(data) == wanted["bytes"] and sha(data) == wanted["sha256"]
        final.append({"path": relative, "bytes": len(data), "sha256": sha(data), "readback_equal": True, **checked})
    assert len(list(DEST.rglob("*"))) >= len(final)
    assert sum(path.is_file() for path in DEST.rglob("*")) == len(final)
    RECEIPT.write_text(json.dumps({"schema": "sn100-dictionary-complete-archive-readback-v1", "status": "passed",
        "archive": DEST.relative_to(REPO).as_posix(), "files_checked": len(final), "raw_files_copied": len(copied),
        "all_lengths_and_sha_equal": True, "files": final, "unknown_non_utf8": [], "byte_exceptions_added": [],
        "archive_handoff_sha256": expected["handoff.json"]["sha256"],
        "archive_manifest_sha256": expected["file_manifest.json"]["sha256"], "build_gui_or_test_executed": False},
        ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": "passed", "archive": str(DEST), "files_readback": len(final),
                      "raw_files": len(copied), "unknown_non_utf8": 0,
                      "handoff_sha256": expected["handoff.json"]["sha256"]}))


if __name__ == "__main__":
    main()
