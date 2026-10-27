"""Host-only ARM build/freeze input checks; no compilation, RK or network."""

import ast
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET


PROGRAM = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("shell_package_inputs", PROGRAM / "shell/package_arm64.py")
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


def pri_files(path, variable):
    """Read the explicit module contracts, not arbitrary qmake programs."""
    text = re.sub(r"\\\s*\n", " ", path.read_text(encoding="utf-8"))
    result = set()
    for line in text.splitlines():
        # Shared value code uses qmake's unique append in both independent modules.
        match = re.match(r"^\s*" + variable + r"\s*(?:\+|\*)=\s*(.*)$", line)
        if not match:
            continue
        for token in match.group(1).split():
            if not token.startswith("$$PWD/"):
                raise ValueError("Expected explicit module input: " + token)
            result.add((path.parent / token[len("$$PWD/"):]).resolve())
    return result



# Independent positive oracle: do not derive it from build.sh or its list output.
LOCAL_FEATURE_INPUTS = {
    "source": (
        "shell/language_shell.cpp", "shell/alarm_shell.cpp",
        "ui/language_setting_page.cpp", "history/report_dictionary_dialog.cpp",
        "alarm_history/alarm_history_store.cpp", "alarm_history/alarm_history_page.cpp",
        "alarm_history/alarm_history_host.cpp", "alarm_history/calendar/cuscalendar.cpp",
        "samples/sample_qc_preset.cpp", "samples/sample_qc_storage.cpp",
        "quality_control/qc_sample_reference_guard.cpp",
        "shell/sample_qc_shell.cpp", "shell/printer_report_shell.cpp",
        "history/result_ingest_store.cpp", "history/result_project_references.cpp",
    ),
    "header": (
        "data/hospital_dictionary_query.h", "ui/language_setting_page.h",
        "history/report_dictionary_dialog.h", "alarm_history/alarm_history_store.h",
        "alarm_history/alarm_history_page.h", "alarm_history/alarm_history_host.h",
        "alarm_history/calendar/cuscalendar.h",
        "samples/sample_qc_preset.h", "samples/sample_qc_protocol.h",
        "shell/sample_qc_shell.h", "shell/printer_report_shell.h",
        "history/result_project_references.h",
    ),
    "form": (
        "ui/frmlanguagesetting.ui", "history/mask_dlg_select.ui",
        "alarm_history/maskdlgabnormal.ui", "alarm_history/cusmaskcalendar.ui",
        "alarm_history/calendar/cuscalendar.ui",
    ),
    "resource": (
        "history/report_dictionary_resources.qrc", "alarm_history/alarm_history_resources.qrc",
    ),
}
LOCAL_FEATURE_HELPER_FORBIDDEN = frozenset(
    Path(name).name for name in LOCAL_FEATURE_INPUTS["source"]
) | frozenset((
    "moc_language_setting_page.cpp", "moc_report_dictionary_dialog.cpp",
    "moc_alarm_history_store.cpp", "moc_alarm_history_page.cpp",
    "moc_alarm_history_host.cpp", "moc_cuscalendar.cpp",
    "qrc_report_dictionary_resources.cpp", "qrc_alarm_history_resources.cpp",
    "history_store.cpp", "report_remaining_store.cpp", "projects_store.cpp",
    "moc_history_store.cpp", "moc_projects_store.cpp", "moc_project_advanced.cpp", "moc_cuscheckbox.cpp",
    "moc_printer_report_shell.cpp",
))


def require_local_feature_inventory(program, inputs, frozen):
    """Reject a missing main input/freeze key or new page input in the helper."""
    for role, names in LOCAL_FEATURE_INPUTS.items():
        for name in names:
            if str(program / name) not in inputs.get(role, ()):
                raise ValueError("Local feature inventory omission: " + role + ":" + name)
            if name not in frozen:
                raise ValueError("Local feature freeze omission: " + name)
    for name in inputs.get("helper_source", ()):
        if Path(name).name in LOCAL_FEATURE_HELPER_FORBIDDEN:
            raise ValueError("Local feature leaked into report helper: " + name)


class ShellArm64InputsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(os.environ.get("SN100_ARM64_INPUT_TEST_DIR", str(
            PROGRAM.parents[1] / "temp/sn100_host/projects/arm64_inputs_test")))
        root.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="input_contract_", dir=root)
        cls.root = Path(cls.temporary.name)
        environment = os.environ.copy()
        environment.update(SN100_ARM64_LIST_INPUTS="1",
                           SN100_ARM64_SYSROOT=str(cls.root / "absent_sysroot"),
                           SN100_ARM64_BUILD_DIR=str(cls.root / "absent_build"))
        result = subprocess.run(["bash", str(PROGRAM / "shell/build.sh"),
                                 str(cls.root / "absent_output/sn100_shell")],
                                env=environment, check=True, text=True, capture_output=True)
        cls.inputs = {}
        for line in result.stdout.splitlines():
            role, value = line.split("\t", 1)
            cls.inputs.setdefault(role, []).append(value)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_inventory_is_read_only_without_sysroot_or_output(self):
        self.assertEqual(list(self.root.iterdir()), [])

    def test_catalog_build_matches_explicit_qmake_modules(self):
        variables = {"source": "SOURCES", "header": "HEADERS", "form": "FORMS", "resource": "RESOURCES"}
        for module, pri in (("projects", "projects_core.pri"), ("reagents", "reagents.pri"),
                            ("samples", "samples_core.pri")):
            for role, variable in variables.items():
                with self.subTest(module=module, role=role):
                    actual = {Path(name) for name in self.inputs[role]
                              if Path(name).is_relative_to(PROGRAM / module)}
                    expected = pri_files(PROGRAM / module / pri, variable)
                    if module == "samples":
                        expected.update(pri_files(PROGRAM / "samples/sample_qc_values.pri", variable))
                    if module == "projects":
                        expected.update(pri_files(PROGRAM / "projects/projects_store_support.pri", variable))
                    self.assertEqual(actual, expected)
        self.assertIn(str(PROGRAM / "shell/catalog_shell.cpp"), self.inputs["source"])
        self.assertIn("-I" + str(PROGRAM / "projects"), self.inputs["include"])
        self.assertIn("-I" + str(PROGRAM / "reagents"), self.inputs["include"])
        self.assertIn("-I" + str(PROGRAM / "samples"), self.inputs["include"])
        self.assertIn("-I" + str(PROGRAM / "samples/material"), self.inputs["include"])
        self.assertTrue(any(name.endswith("/QtSvg") for name in self.inputs["include"]))

    def test_status_build_and_freeze_cover_replay_and_restricted_loopback(self):
        snapshot = package.snapshot(PROGRAM)
        for stem in ('status_replay', 'status_session', 'frame_codec',
                     'status_loopback', 'read_only_client', 'tcp_transport', 'read_only_messages'):
            for suffix, role in (('.cpp', 'source'), ('.h', 'header')):
                path = PROGRAM / 'protocol' / (stem + suffix)
                self.assertIn(str(path), self.inputs[role])
                self.assertEqual(snapshot[path.relative_to(PROGRAM).as_posix()], package.sha(path))
        sources = {Path(name).name for name in self.inputs['source']}
        self.assertIn('device_status_shell.cpp', sources)
        self.assertNotIn('soak_main.cpp', sources)
        self.assertNotIn('protocol/soak_main.cpp', snapshot)

    def test_instrument_build_freeze_and_startup_include_offline_module(self):
        snapshot = package.snapshot(PROGRAM)
        variables = {"source": "SOURCES", "header": "HEADERS", "form": "FORMS", "resource": "RESOURCES"}
        for role, variable in variables.items():
            expected = set()
            for pri in ('instrument_widgets.pri', 'maintenance_core.pri'):
                expected.update(pri_files(PROGRAM / 'instrument' / pri, variable))
            actual = {Path(name) for name in self.inputs[role]
                      if Path(name).is_relative_to(PROGRAM / 'instrument')}
            self.assertEqual(actual, expected)
            for path in expected:
                self.assertEqual(snapshot[path.relative_to(PROGRAM).as_posix()], package.sha(path))
        self.assertIn(str(PROGRAM / 'shell/instrument_shell.cpp'), self.inputs['source'])
        self.assertIn('--instrument', (PROGRAM / 'shell/wsl.sh').read_text())
        self.assertIn('--instrument', (PROGRAM / 'shell/rk_board.sh').read_text())

    def test_main_does_not_compile_independent_entries_or_tests(self):
        for role in ("source", "form", "header", "resource"):
            names = self.inputs[role]
            self.assertEqual(len(names), len(set(names)), role + " contains duplicate inputs")
            for name in names:
                path = Path(name)
                self.assertTrue(path.is_file(), name)
                self.assertNotIn("tests", path.relative_to(PROGRAM).parts, name)
                if path.name == "main.cpp":
                    self.assertEqual(path, PROGRAM / "shell/main.cpp")

    def test_password_login_build_freeze_and_offline_scope(self):
        frozen = package.snapshot(PROGRAM)
        for role, name in (("source", "shell/login_shell.cpp"), ("source", "ui/maskdlglogin.cpp"),
                           ("header", "ui/maskdlglogin.h"), ("form", "ui/maskdlglogin.ui")):
            path = PROGRAM / name
            self.assertIn(str(path), self.inputs[role])
            self.assertEqual(frozen[name], package.sha(path))
        tree = ast.parse((PROGRAM / "shell/package_arm64.py").read_text(encoding="utf-8"))
        info = next(node.value for node in ast.walk(tree)
                    if isinstance(node, ast.Assign) and any(
                        isinstance(target, ast.Name) and target.id == "info" for target in node.targets))
        fields = {key.value: value for key, value in zip(info.keys, info.values)
                  if isinstance(key, ast.Constant)}
        self.assertIn("--password-login", ast.literal_eval(fields["startup_feature_arguments"]))
        self.assertEqual(ast.literal_eval(fields["password_login_scope"]), {
            "storage": "isolated_local_test_accounts", "default_session": "Guest",
            "instrument_connection": False, "device_action_condition_changed": False,
            "production_credential_security_verified": False})

    def test_printer_settings_build_freeze_and_local_only_scope(self):
        frozen = package.snapshot(PROGRAM)
        for role, name in (("source", "shell/printer_shell.cpp"),
                           ("source", "shell/printer_report_shell.cpp"),
                           ("header", "shell/printer_report_shell.h"),
                           ("source", "data/printer_settings_store.cpp"),
                           ("source", "ui/frmprintersetting.cpp"),
                           ("header", "data/printer_settings_store.h"),
                           ("header", "ui/frmprintersetting.h"),
                           ("form", "ui/frmprintersetting.ui")):
            path = PROGRAM / name
            self.assertIn(str(path), self.inputs[role])
            self.assertEqual(frozen[name], package.sha(path))
        self.assertIn("--printer-settings", (PROGRAM / "shell/wsl.sh").read_text())
        self.assertIn('"local_printer_settings"', (PROGRAM / "shell/rk_board.sh").read_text())
        info = ast.parse((PROGRAM / "shell/package_arm64.py").read_text(encoding="utf-8"))
        declaration = next(node.value for node in ast.walk(info)
                           if isinstance(node, ast.Assign) and any(
                               isinstance(target, ast.Name) and target.id == "info" for target in node.targets))
        fields = {key.value: value for key, value in zip(declaration.keys, declaration.values)
                  if isinstance(key, ast.Constant)}
        self.assertEqual(ast.literal_eval(fields["printer_settings_scope"]), {
            "storage": "explicit_isolated_settings_directory", "key": "printer/name",
            "print_job_submitted": False, "system_default_changed": False})
        self.assertIn("configured_report_native_print", ast.literal_eval(fields["capabilities"]))
        self.assertEqual(ast.literal_eval(fields["printer_application_scope"]), {
            "configuration": "committed_store_snapshot", "device": "exact_named_or_explicit_dialog_choice",
            "confirmation": "standard_dialog_in_isolated_helper", "automatic_submit_or_retry": False,
            "submitted_means": "software_handoff_only", "physical_output_verified": False,
            "rk_gui_backend_verified": False})
        main = (PROGRAM / "shell/main.cpp").read_text(encoding="utf-8")
        self.assertIn("if (printer && shell->historyReports())", main)
        self.assertIn("setReportController(shell->historyReports())", main)


    def test_lis_local_settings_build_and_release_scope(self):
        frozen = package.snapshot(PROGRAM)
        for role, names in {
            "source": ("shell/lis_shell.cpp", "data/lis_settings_store.cpp", "data/serial_port_catalog.cpp", "ui/frmlissetting.cpp"),
            "header": ("data/lis_settings_store.h", "data/serial_port_catalog.h", "ui/frmlissetting.h"),
            "form": ("ui/frmlissetting.ui",),
        }.items():
            for name in names:
                self.assertIn(str(PROGRAM / name), self.inputs[role])
                self.assertEqual(frozen[name], package.sha(PROGRAM / name))
        self.assertIn("--lis-settings", (PROGRAM / "shell/wsl.sh").read_text())
        self.assertIn('"local_lis_settings"', (PROGRAM / "shell/rk_board.sh").read_text())
        info = ast.parse((PROGRAM / "shell/package_arm64.py").read_text(encoding="utf-8"))
        declaration = next(node.value for node in ast.walk(info)
                           if isinstance(node, ast.Assign) and any(
                               isinstance(target, ast.Name) and target.id == "info" for target in node.targets))
        fields = {key.value: value for key, value in zip(declaration.keys, declaration.values)
                  if isinstance(key, ast.Constant)}
        self.assertIn("local_lis_settings", ast.literal_eval(fields["capabilities"]))
        self.assertIn("--lis-settings", ast.literal_eval(fields["startup_feature_arguments"]))
        self.assertEqual(ast.literal_eval(fields["lis_settings_scope"]), {
            "storage": "explicit_isolated_settings_directory", "communication": False,
            "serial_device_open": False, "auto_flags": "intent_only"})

    def test_report_helper_keeps_only_rendering_and_readonly_value_text(self):
        expected = {PROGRAM / "history" / name for name in (
            "history_value_text.cpp", "report_document.cpp", "report_render.cpp",
            "report_renderer.cpp", "report_pdf_mapping.cpp")}
        for folder in ("QtRPT", "CommonFiles"):
            expected.update((PROGRAM / "thirdparty/qtrpt" / folder).glob("*.cpp"))
        self.assertEqual({Path(name) for name in self.inputs["helper_source"]}, expected)
        value_text = PROGRAM / "history/history_value_text.cpp"
        self.assertIn(str(value_text), self.inputs["source"])
        self.assertEqual(package.snapshot(PROGRAM)["history/history_value_text.cpp"], package.sha(value_text))

    def test_quality_control_closure_and_reference_scope(self):
        frozen = package.snapshot(PROGRAM)
        variables = {"source": "SOURCES", "header": "HEADERS", "form": "FORMS", "resource": "RESOURCES"}
        for role, variable in variables.items():
            expected = set()
            for pri in ('qc_core.pri', 'qc_widgets.pri'):
                expected.update(pri_files(PROGRAM / 'quality_control' / pri, variable))
            actual = {Path(name) for name in self.inputs[role]
                      if any(Path(name).is_relative_to(PROGRAM / module)
                             for module in ('quality_control', 'quality'))}
            self.assertEqual(actual, expected)
            for path in expected:
                self.assertEqual(frozen[path.relative_to(PROGRAM).as_posix()], package.sha(path))
        self.assertIn(str(PROGRAM / 'shell/quality_shell.cpp'), self.inputs['source'])
        self.assertNotIn(str(PROGRAM / 'quality_control/main.cpp'), self.inputs['source'])
        self.assertIn('--quality-control', (PROGRAM / 'shell/main.cpp').read_text(encoding='utf-8'))

    def test_calibration_local_closure_and_startup(self):
        frozen = package.snapshot(PROGRAM)
        variables = {"source": "SOURCES", "header": "HEADERS", "form": "FORMS", "resource": "RESOURCES"}
        for role, variable in variables.items():
            expected = pri_files(PROGRAM / 'calibration/calibration.pri', variable)
            actual = {Path(name) for name in self.inputs[role]
                      if Path(name).is_relative_to(PROGRAM / 'calibration')}
            self.assertEqual(actual, expected)
            for path in expected:
                self.assertEqual(frozen[path.relative_to(PROGRAM).as_posix()], package.sha(path))
        self.assertIn(str(PROGRAM / 'shell/calibration_shell.cpp'), self.inputs['source'])
        self.assertNotIn(str(PROGRAM / 'calibration/main.cpp'), self.inputs['source'])
        self.assertIn('--calibration', (PROGRAM / 'shell/main.cpp').read_text(encoding='utf-8'))
        self.assertIn('--calibration', (PROGRAM / 'shell/wsl.sh').read_text(encoding='utf-8'))
        self.assertIn('"calibration_local"', (PROGRAM / 'shell/rk_board.sh').read_text(encoding='utf-8'))

    def test_freeze_covers_catalog_code_forms_qrc_and_all_qrc_members(self):
        snapshot = package.snapshot(PROGRAM)
        for role in ("source", "header", "form", "resource"):
            for name in self.inputs[role]:
                path = Path(name)
                if any(path.is_relative_to(PROGRAM / module) for module in
                       ("projects", "reagents", "samples", "quality_control", "quality", "calibration")):
                    self.assertIn(path.relative_to(PROGRAM).as_posix(), snapshot)
        for name in self.inputs["resource"]:
            qrc = Path(name)
            if qrc.parent.name not in ("projects", "reagents", "samples", "quality_control", "calibration"):
                continue
            for entry in ET.parse(qrc).findall(".//file"):
                member = (qrc.parent / entry.text).resolve(strict=True)
                self.assertIn(member.relative_to(PROGRAM).as_posix(), snapshot)

    def test_changed_and_new_catalog_files_invalidate_a_snapshot(self):
        with tempfile.TemporaryDirectory(prefix="snapshot_", dir=self.root) as temporary:
            program = Path(temporary)
            fixed = ("prepare_arm64.sh", "resources.qrc", "tests/fixtures/accounts.json",
                     "quality/statistics.h", "quality/statistics.cpp",
                     "tests/fixtures/history_reports.json", "tests/fixtures/history_report_options.json",
                     "protocol/status_replay.h", "protocol/status_replay.cpp", "protocol/status_session.h",
                     "protocol/status_session.cpp", "protocol/frame_codec.h", "protocol/frame_codec.cpp",
                     "protocol/status_loopback.h", "protocol/status_loopback.cpp", "protocol/read_only_client.h",
                     "protocol/read_only_client.cpp", "protocol/tcp_transport.h", "protocol/tcp_transport.cpp",
                     "protocol/read_only_messages.h", "protocol/read_only_messages.cpp")
            for name in fixed + ("projects/project_page.cpp", "reagents/reagent_store.cpp", "samples/sample_store.cpp"):
                path = program / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("explicit anonymous input\n", encoding="utf-8")
            before = package.snapshot(program)
            changed = program / "projects/project_page.cpp"
            changed.write_text("changed formal project input\n", encoding="utf-8")
            after = package.snapshot(program)
            self.assertNotEqual(before["projects/project_page.cpp"], after["projects/project_page.cpp"])
            sample = program / "samples/sample_store.cpp"
            sample.write_text("changed local sample preset input\n", encoding="utf-8")
            self.assertNotEqual(before["samples/sample_store.cpp"], package.snapshot(program)["samples/sample_store.cpp"])
            replay = program / "protocol/status_replay.cpp"
            replay.write_text("changed offline replay input\n", encoding="utf-8")
            self.assertNotEqual(before["protocol/status_replay.cpp"], package.snapshot(program)["protocol/status_replay.cpp"])
            added = program / "reagents/new_form.ui"
            added.write_text("<ui/>\n", encoding="utf-8")
            self.assertNotIn("reagents/new_form.ui", before)
            self.assertIn("reagents/new_form.ui", package.snapshot(program))
            sample_form = program / "samples/new_form.ui"
            sample_form.write_text("<ui/>\n", encoding="utf-8")
            self.assertNotIn("samples/new_form.ui", before)
            self.assertIn("samples/new_form.ui", package.snapshot(program))

    def test_package_refuses_mismatched_catalog_freeze_before_creating_stage(self):
        with tempfile.TemporaryDirectory(prefix="reject_", dir=self.root) as temporary:
            work = Path(temporary)
            frozen = package.snapshot(PROGRAM)
            frozen["samples/sample_page.cpp"] = "0" * 64
            snapshot = work / "source_snapshot.json"
            snapshot.write_text(json.dumps(frozen), encoding="utf-8")
            output = work / "packages"
            arguments = [str(PROGRAM / "shell/package_arm64.py"), "package", "--output", str(output),
                         "--binary", str(work / "missing_binary"), "--sysroot", str(work / "missing_sysroot"),
                         "--snapshot", str(snapshot), "--dependencies", str(work / "missing_dependencies.json")]
            with mock.patch.object(sys, "argv", arguments):
                with self.assertRaisesRegex(RuntimeError, "Source inputs changed"):
                    package.main()
            self.assertFalse(output.exists())

    def test_sample_release_metadata_limits_tasks_to_local_presets(self):
        tree = ast.parse((PROGRAM / "shell/package_arm64.py").read_text(encoding="utf-8"))
        info = next(node.value for node in ast.walk(tree)
                    if isinstance(node, ast.Assign) and any(
                        isinstance(target, ast.Name) and target.id == "info" for target in node.targets))
        fields = {key.value: value for key, value in zip(info.keys, info.values)
                  if isinstance(key, ast.Constant)}
        capabilities = ast.literal_eval(fields["capabilities"])
        self.assertIn("sample_local_presets", capabilities)
        self.assertIn("sample_local_tasks", capabilities)
        self.assertIn("sample_local_presets", ast.literal_eval(fields["pages"]))
        self.assertEqual(ast.literal_eval(fields["sample_task_scope"]), {
            "storage": "local_presets", "instrument_submission": False, "instrument_execution": False})
        startup = ast.literal_eval(fields["startup_feature_arguments"])
        self.assertIn("--catalog", startup)
        self.assertIn("--samples", startup)
        self.assertEqual(ast.literal_eval(fields["sample_ui_dependencies"]), {
            "library": "libQt5Svg.so.5", "image_plugin": "plugins/imageformats/libqsvg.so",
            "original_qss_svg": ":/image/arrow_drop_down.svg", "license": "licenses/QtSvgCopyright.txt"})
        private = next(node.value for node in ast.walk(tree)
                       if isinstance(node, ast.Assign) and any(
                           isinstance(target, ast.Name) and target.id == "private" for target in node.targets))
        self.assertEqual(ast.literal_eval(private)[
            "usr/lib/aarch64-linux-gnu/qt5/plugins/imageformats/libqsvg.so"], "plugins/imageformats/libqsvg.so")


    def test_local_feature_main_inputs_freeze_and_helper_isolation(self):
        frozen = package.snapshot(PROGRAM)
        require_local_feature_inventory(PROGRAM, self.inputs, frozen)
        for role, names in LOCAL_FEATURE_INPUTS.items():
            for name in names:
                with self.subTest(role=role, name=name):
                    self.assertEqual(frozen[name], package.sha(PROGRAM / name))
        for folder in ("alarm_history", "alarm_history/calendar"):
            self.assertIn("-I" + str(PROGRAM / folder), self.inputs["include"])
        for name in LOCAL_FEATURE_INPUTS["resource"]:
            qrc = PROGRAM / name
            for entry in ET.parse(qrc).findall(".//file"):
                member = (qrc.parent / entry.text).resolve(strict=True)
                relative = member.relative_to(PROGRAM).as_posix()
                self.assertEqual(frozen[relative], package.sha(member))

    def test_local_feature_oracle_rejects_bounded_private_omissions_and_helper_leaks(self):
        # Pure in-memory copies; no production source deletion or shell mutation.
        frozen = package.snapshot(PROGRAM)
        require_local_feature_inventory(PROGRAM, self.inputs, frozen)
        for role, names in LOCAL_FEATURE_INPUTS.items():
            for name in names:
                with self.subTest(omitted_role=role, name=name):
                    private = {key: list(values) for key, values in self.inputs.items()}
                    private[role] = [value for value in private[role] if value != str(PROGRAM / name)]
                    with self.assertRaisesRegex(ValueError, "Local feature inventory omission"):
                        require_local_feature_inventory(PROGRAM, private, frozen)
                with self.subTest(omitted_freeze_key=name):
                    private_frozen = dict(frozen)
                    del private_frozen[name]
                    with self.assertRaisesRegex(ValueError, "Local feature freeze omission"):
                        require_local_feature_inventory(PROGRAM, self.inputs, private_frozen)
        for name in sorted(LOCAL_FEATURE_HELPER_FORBIDDEN):
            with self.subTest(helper_leak=name):
                private = {key: list(values) for key, values in self.inputs.items()}
                # Generated names here are synthetic oracle inputs, not a claim
                # that list-only mode generated/compiled these files.
                private.setdefault("helper_source", []).append(str(self.root / name))
                with self.assertRaisesRegex(ValueError, "Local feature leaked into report helper"):
                    require_local_feature_inventory(PROGRAM, private, frozen)

    def test_alarm_directory_changed_and_new_files_invalidate_a_snapshot(self):
        with tempfile.TemporaryDirectory(prefix="alarm_snapshot_", dir=self.root) as temporary:
            program = Path(temporary)
            # Only the explicit files required by snapshot(), no formal-tree copy.
            fixed = ("prepare_arm64.sh", "resources.qrc", "quality/statistics.h", "quality/statistics.cpp",
                     "tests/fixtures/accounts.json", "tests/fixtures/history_reports.json",
                     "tests/fixtures/history_report_options.json",
                     "protocol/status_replay.h", "protocol/status_replay.cpp",
                     "protocol/status_session.h", "protocol/status_session.cpp",
                     "protocol/frame_codec.h", "protocol/frame_codec.cpp",
                     "protocol/status_loopback.h", "protocol/status_loopback.cpp",
                     "protocol/read_only_client.h", "protocol/read_only_client.cpp",
                     "protocol/tcp_transport.h", "protocol/tcp_transport.cpp",
                     "protocol/read_only_messages.h", "protocol/read_only_messages.cpp")
            alarm = ("alarm_history/alarm_history_store.cpp", "alarm_history/alarm_history_store.h",
                     "alarm_history/maskdlgabnormal.ui", "alarm_history/alarm_history_resources.qrc",
                     "alarm_history/resources/calendar_ico.png")
            for name in fixed + alarm:
                path = program / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"explicit anonymous private snapshot input\n")
            before = package.snapshot(program)
            self.assertTrue(set(alarm).issubset(before))
            for name in alarm:
                with self.subTest(changed_alarm_input=name):
                    path = program / name
                    path.write_bytes(b"changed anonymous private alarm input\n")
                    self.assertNotEqual(before[name], package.snapshot(program)[name])
            added = program / "alarm_history/calendar/new_private_form.ui"
            added.parent.mkdir(parents=True, exist_ok=True)
            added.write_text("<ui/>\n", encoding="utf-8")
            self.assertNotIn("alarm_history/calendar/new_private_form.ui", before)
            self.assertEqual(package.snapshot(program)["alarm_history/calendar/new_private_form.ui"],
                             package.sha(added))



if __name__ == "__main__":
    unittest.main()
