"""Host-only failure-path tests; no compiler, target execution or network."""

import copy
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest


CHECKER = Path(__file__).resolve().parents[1] / "shell" / "check_arm64.py"
SPEC = importlib.util.spec_from_file_location("arm64_check", CHECKER)
arm64 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(arm64)


def elf(needed=(), definitions=(), requirements=None, interpreter=None, machine="AArch64"):
    return {"class": "ELF64", "data": "2's complement, little endian", "machine": machine,
            "needed": list(needed), "definitions": list(definitions), "requirements": requirements or {},
            "interpreter": interpreter, "soname": None, "search_paths": []}


class DependencyCheckTest(unittest.TestCase):
    def setUp(self):
        task_temp = Path(os.environ.get("SN100_ARM64_CHECK_TEST_DIR", str(Path(__file__).resolve().parents[3] / "temp/sn100_host/rk_prepare/dependency_test")))
        task_temp.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="checker_fixture_", dir=task_temp)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.sysroot = self.root / "sysroot"
        self.sysroot.mkdir()
        self.binary = self.root / "sn100_shell"
        self.metadata = {}
        self.add(self.binary, elf(["libQt5Sql.so.5", "libc.so.6"], interpreter="/lib/ld-linux-aarch64.so.1"))
        self.add_target("/lib/ld-linux-aarch64.so.1", elf(definitions=["GLIBC_2.17"]))
        self.add_target("/lib/aarch64-linux-gnu/libc.so.6", elf(definitions=["GLIBC_2.17", "GLIBC_2.34"]))
        self.add_target("/usr/lib/aarch64-linux-gnu/libQt5Sql.so.5", elf())
        self.sqlite = self.add_target("/" + arm64.PLUGIN_ROOT + "/sqldrivers/libqsqlite.so", elf(["libsqlite3.so.0"]))
        self.sqlite_library = self.add_target("/lib/aarch64-linux-gnu/libsqlite3.so.0", elf())

    def add(self, path, info):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"explicit non-executable fixture " + path.name.encode())
        self.metadata[str(path)] = info
        return path

    def add_target(self, target, info):
        return self.add(self.sysroot / target.lstrip("/"), info)

    def run_check(self):
        reader = lambda path: copy.deepcopy(self.metadata[str(path)])
        return arm64.Checker(self.binary, self.sysroot, reader).run()

    def test_report_helper_requires_its_transitive_libraries(self):
        helper = self.add(self.root / "sn100_report_renderer", elf(["libReportOnly.so.1"], interpreter="/lib/ld-linux-aarch64.so.1"))
        reader = lambda path: copy.deepcopy(self.metadata[str(path)])
        report = arm64.Checker(self.binary, self.sysroot, reader, [helper]).run()
        self.assertTrue(any(item["code"] == "missing_dependency" and item["library"] == "libReportOnly.so.1" for item in report["errors"]))
        self.add_target("/usr/lib/aarch64-linux-gnu/libReportOnly.so.1", elf())
        self.add_target("/" + arm64.PLUGIN_ROOT + "/platforms/libqoffscreen.so", elf())
        report = arm64.Checker(self.binary, self.sysroot, reader, [helper]).run()
        self.assertTrue(report["offline_link_and_sql_closure_passed"])

    def test_report_helper_requires_offscreen_platform_plugin(self):
        helper = self.add(self.root / "sn100_report_renderer", elf(interpreter="/lib/ld-linux-aarch64.so.1"))
        reader = lambda path: copy.deepcopy(self.metadata[str(path)])
        report = arm64.Checker(self.binary, self.sysroot, reader, [helper]).run()
        self.assertTrue(any(item["code"] == "missing_report_offscreen_plugin" for item in report["errors"]))

    def test_missing_report_helper_rejects_complete_main(self):
        reader = lambda path: copy.deepcopy(self.metadata[str(path)])
        report = arm64.Checker(self.binary, self.sysroot, reader, [self.root / "missing_renderer"]).run()
        self.assertTrue(any(item["code"] == "missing_additional_binary" for item in report["errors"]))

    def test_wrong_report_helper_architecture_rejects_release(self):
        helper = self.add(self.root / "wrong_renderer", elf(machine="Advanced Micro Devices X86-64"))
        reader = lambda path: copy.deepcopy(self.metadata[str(path)])
        report = arm64.Checker(self.binary, self.sysroot, reader, [helper]).run()
        self.assertTrue(any(item["code"] == "wrong_elf_architecture" for item in report["errors"]))

    def test_complete_main_and_sqlite_can_pass_with_board_graphics_pending(self):
        report = self.run_check()
        self.assertTrue(report["offline_link_and_sql_closure_passed"])
        self.assertFalse(report["board_runtime_verified"])
        self.assertEqual(report["runtime_components"]["eglfs_platform"]["status"], "not_archived_board_check_required")

    def test_transitive_sqlite_library_is_required_even_if_main_links(self):
        self.sqlite_library.unlink()
        report = self.run_check()
        self.assertFalse(report["offline_link_and_sql_closure_passed"])
        self.assertTrue(any(item["code"] == "missing_dependency" and item["library"] == "libsqlite3.so.0" for item in report["errors"]))

    def test_sql_plugin_missing_is_a_failure(self):
        self.sqlite.unlink()
        self.assertIn("missing_sqlite_plugin", [item["code"] for item in self.run_check()["errors"]])

    def test_version_must_be_defined_by_the_specific_provider(self):
        self.metadata[str(self.binary)]["requirements"] = {"libc.so.6": [{"name": "GLIBC_2.28", "flags": "none"}]}
        # libc defines a newer version but the requested one is absent.
        self.assertIn("missing_symbol_version", [item["code"] for item in self.run_check()["errors"]])

    def test_missing_loader_fails_without_using_host_loader(self):
        (self.sysroot / "lib/ld-linux-aarch64.so.1").unlink()
        self.assertIn("missing_interpreter", [item["code"] for item in self.run_check()["errors"]])

    def test_architecture_is_checked_for_plugins_too(self):
        self.metadata[str(self.sqlite)]["machine"] = "Advanced Micro Devices X86-64"
        self.assertIn("wrong_elf_architecture", [item["code"] for item in self.run_check()["errors"]])

    def test_absolute_target_symlink_is_anchored_in_sysroot(self):
        # Remove the earlier search-directory match so the absolute alias is used.
        self.sqlite_library.unlink()
        actual = self.add_target("/opt/sqlite/libsqlite3.so.0", elf())
        link = self.sysroot / "usr/lib/aarch64-linux-gnu/libsqlite3.so.0"
        link.symlink_to("/opt/sqlite/libsqlite3.so.0")
        report = self.run_check()
        self.assertTrue(report["offline_link_and_sql_closure_passed"])
        self.assertTrue(any(item["provider"] == str(actual) for item in report["dependency_edges"]))

    def test_relative_symlink_cannot_escape_sysroot(self):
        link = self.sysroot / "escape"
        link.symlink_to("../outside")
        checker = arm64.Checker(self.binary, self.sysroot, lambda path: {})
        with self.assertRaisesRegex(ValueError, "escapes sysroot"):
            checker.target_file("/escape")

    def test_present_eglfs_plugin_closure_is_inspected(self):
        self.add_target("/" + arm64.PLUGIN_ROOT + "/platforms/libqeglfs.so", elf(["libMissingEgl.so.1"]))
        self.assertTrue(any(item.get("library") == "libMissingEgl.so.1" for item in self.run_check()["errors"]))

    def test_loader_libc_dependency_cycle_is_inspected_once(self):
        self.metadata[str(self.sysroot / "lib/ld-linux-aarch64.so.1")]["needed"] = ["libc.so.6"]
        self.metadata[str(self.sysroot / "lib/aarch64-linux-gnu/libc.so.6")]["needed"] = ["ld-linux-aarch64.so.1"]
        self.assertTrue(self.run_check()["offline_link_and_sql_closure_passed"])

    @unittest.skipUnless(Path("/bin/true").is_file() and shutil.which("readelf"), "WSL readelf integration")
    def test_real_x86_elf_is_rejected_and_reports_are_hashed(self):
        empty = self.root / "empty_sysroot"
        empty.mkdir()
        output = self.root / "real_elf_report"
        with contextlib.redirect_stdout(io.StringIO()):
            status = arm64.main(["/bin/true", "--sysroot", str(empty), "--output-dir", str(output)])
        self.assertEqual(status, 1)
        report = json.loads((output / "dependency_report.json").read_text())
        self.assertIn("wrong_elf_architecture", [item["code"] for item in report["errors"]])
        for entry in (output / "SHA256SUMS").read_text().splitlines():
            digest, path = entry.split("  ", 1)
            self.assertEqual(digest, arm64.sha256(Path(path)))

    def test_needs_are_not_accidentally_treated_as_definitions(self):
        output = """  Class: ELF64
  Data: 2's complement, little endian
  Machine: AArch64
Version definition section '.gnu.version_d' contains 1 entries:
  0x0000: Rev: 1 Flags: none Index: 2 Cnt: 1 Name: Qt_5
Version needs section '.gnu.version_r' contains 1 entries:
  000000: Version: 1 File: libc.so.6 Cnt: 1
  0x0010: Name: GLIBC_2.34 Flags: none Version: 3
"""
        info = arm64.parse_readelf(output)
        self.assertEqual(info["definitions"], ["Qt_5"])
        self.assertEqual(info["requirements"]["libc.so.6"][0]["name"], "GLIBC_2.34")


if __name__ == "__main__":
    unittest.main()
