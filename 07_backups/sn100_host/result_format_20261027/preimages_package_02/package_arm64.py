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
    for name in ("shell", "data", "ui", "history", "projects", "reagents", "samples", "instrument", "quality_control", "calibration", "alarm_history", "lis_raw_inbox", "resources", "thirdparty/qtrpt"):
        paths.extend(p for p in (program / name).rglob("*")
                     if p.is_file() and p.suffix not in (".md", ".pyc")
                     and "__pycache__" not in p.parts)
    paths.extend(program / name for name in ("prepare_arm64.sh", "resources.qrc",
                 "quality/statistics.h", "quality/statistics.cpp",
                 "tests/fixtures/accounts.json", "tests/fixtures/history_reports.json",
                 "tests/fixtures/history_report_options.json"))
    # Freeze the linked replay and self-owned loopback-only read-only sources.
    paths.extend(program / 'protocol' / name for name in (
        'status_replay.h', 'status_replay.cpp', 'status_session.h', 'status_session.cpp',
        'frame_codec.h', 'frame_codec.cpp', 'status_loopback.h', 'status_loopback.cpp',
        'read_only_client.h', 'read_only_client.cpp', 'tcp_transport.h', 'tcp_transport.cpp',
        'read_only_messages.h', 'read_only_messages.cpp'))
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
        "usr/lib/aarch64-linux-gnu/qt5/plugins/imageformats/libqsvg.so": "plugins/imageformats/libqsvg.so",
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
    shutil.copy2(sysroot / "usr/share/doc/libqt5svg5/copyright", stage / "licenses/QtSvgCopyright.txt")
    for package in ("libfontconfig1", "libexpat1"):
        shutil.copy2(sysroot / "usr/share/doc" / package / "copyright", stage / "licenses" / (package + ".txt"))
    (stage / "fonts/fonts.conf").write_text(
        '<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">\n'
        '<fontconfig><include ignore_missing="yes">/etc/fonts/fonts.conf</include>'
        '<dir prefix="relative">.</dir><cachedir prefix="xdg">fontconfig</cachedir></fontconfig>\n', encoding="utf-8")
    info = {"schema": "sn100-offline-rk-release-v1", "binary_sha256": sha(args.binary),
            "renderer_sha256": sha(renderer), "capabilities": ["report_preview_pdf", "report_html", "report_local_hide", "report_display_settings", "project_unit_basis", "project_advanced_local", "reagent_basis", "reagent_batch_metadata_local", "sample_local_presets", "sample_local_tasks", "offline_status_replay", "offline_status_loopback", "instrument_offline_maintenance", "local_password_login", "local_printer_settings", "configured_report_native_print", "local_lis_settings", "lis_raw_inbox_local", "sample_combination_mode_settings", "quality_control_local", "calibration_local", "alarm_history_readonly", "language_page_cn_only", "report_dictionary_readonly"],
            "report_template": {"path": ":/history_report/report.xml", "sha256": sha(program / "history/templates/report.xml"), "embedded": True},
            "fonts": fonts, "dependencies_sha256": sha(args.dependencies),
            "architecture": "aarch64", "mode": "offline", "pages": ["hospital", "account", "history_csv", "report_preview_pdf", "project_unit_basis", "reagent_basis", "sample_local_presets", "instrument_offline_maintenance", "quality_control_local", "calibration_local", "alarm_history_readonly", "language_page_cn_only", "report_dictionary_readonly", "lis_raw_inbox_local"],
            "startup_feature_arguments": ["--accounts", "--history", "--catalog", "--samples", "--instrument", "--password-login", "--printer-settings", "--lis-settings", "--quality-control", "--calibration", "--alarm-history", "--lis-raw-inbox"],
            "alarm_history_scope": {"source": "explicit_existing_readonly_file", "default_relative_path": "alarm_history/abnormal_log.db", "schema_create_or_migrate": False, "live_alarm_count_or_severity": False, "device_query_reset_beep_ack_interlocks": False},
            "language_page_scope": {"display": "original_cn_only_form", "apply": "asynchronous_unsupported_notice", "setting_written": False, "whole_app_switching": False},
            "report_dictionary_scope": {"source": "committed_shared_hospital_dictionary", "read_only": True, "patient_or_dictionary_permissions_changed": False},
            "quality_control_scope": {"storage": "explicit_isolated_quality_control_directory", "project_reference": "durable_catalog_reservation_then_local_commit", "clinical_rules": False, "instrument_task": False},
            "calibration_scope": {"storage": "explicit_isolated_calibration_directory", "curve": "raw_points_only", "fitting": False, "approval": False, "apply_to_instrument": False},
            "project_advanced_scope": {"storage": "local_catalog_metadata", "device_execution": False, "algorithm_parameters_verified": False},
            "report_remaining_scope": {"delete": "soft_hide_preserves_results", "display": "preferences_do_not_rewrite_history", "html": "original_template_file_export", "xlsx": False, "recalculation": False, "upload": False},
            "lis_raw_inbox_scope": {"storage": "explicit_isolated_lis_raw_inbox_directory", "default_relative_path": "lis_raw_inbox",
                                    "startup": "explicit_flag_only_requires_accounts_and_lis_settings", "default_enabled": False,
                                    "content": "original_utf8_single_mllp_dsr_unmapped_not_applied", "safe_file_import": "Linux_only",
                                    "network": False, "ACK": False, "task_application": False, "Windows_safe_import": False,
                                    "close": "actual_store_closed_then_thread_join_ACK_before_main_window_exit"},
            "sample_combination_mode_scope": {"storage": "catalog_mode_table_same_transaction_as_ordered_members",
                                              "modes": ["ordinary", "multi_reagent_bottle"], "bottle_task_expansion": False,
                                              "multi_apply": "explicit_refusal_preserves_rack_draft", "clinical_QC": False},
            "lis_settings_scope": {"storage": "explicit_isolated_settings_directory", "communication": False, "serial_device_open": False, "auto_flags": "intent_only"},
            "printer_settings_scope": {"storage": "explicit_isolated_settings_directory", "key": "printer/name",
                                       "print_job_submitted": False, "system_default_changed": False},
            "printer_application_scope": {"configuration": "committed_store_snapshot", "device": "exact_named_or_explicit_dialog_choice",
                                          "confirmation": "standard_dialog_in_isolated_helper", "automatic_submit_or_retry": False,
                                          "submitted_means": "software_handoff_only", "physical_output_verified": False,
                                          "rk_gui_backend_verified": False},
            "password_login_scope": {"storage": "isolated_local_test_accounts", "default_session": "Guest",
                                     "instrument_connection": False, "device_action_condition_changed": False,
                                     "production_credential_security_verified": False},
            "sample_task_scope": {"storage": "local_presets", "instrument_submission": False, "instrument_execution": False},
            "status_loopback_scope": {"endpoint": "self_owned_127.0.0.1_ephemeral_port", "request": "Qj",
                                      "external_endpoint": False, "instrument_execution": False, "default_enabled": False},
            "sample_ui_dependencies": {"library": "libQt5Svg.so.5", "image_plugin": "plugins/imageformats/libqsvg.so",
                                       "original_qss_svg": ":/image/arrow_drop_down.svg", "license": "licenses/QtSvgCopyright.txt"},
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
