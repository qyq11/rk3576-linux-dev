"""Fingerprint-policy tests only; fixtures are not compiled ARM executables."""
from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

PROGRAM = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("arm64_reuse_policy", PROGRAM / "shell/arm64_object_reuse.py")
reuse = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(reuse)


class ObjectReuseTest(unittest.TestCase):
    def setUp(self):
        work = PROGRAM.parents[1] / "temp/sn100_host/projects/reuse_policy_tests"
        work.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=work)
        self.root = Path(self.temporary.name)
        self.program = self.root / "program"
        self.build = self.root / "build"
        self.build.mkdir()
        self.names = ["samples/sample_page.cpp", "stable.cpp", "stable.h", "form.ui", "resources.qrc",
                      "resources/icon.svg", "tests/fixtures/example.json", "shell/build.sh"]
        for name in self.names:
            target = self.program / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("// anonymous fingerprint fixture\n")
        self.sources = [str(self.program / name) for name in self.names[:2]]
        self.compiler = self.root / "compiler"
        self.compiler.write_text("pinned compiler fingerprint fixture\n")
        self.compiler.chmod(0o700)
        self.flags = ["-std=c++14", "-O2", "-I" + str(self.program)]
        self.manifest = self.root / "objects.json"
        header = self.build / "ui_fixture.h"
        header.write_text("// generated UI header fingerprint fixture\n")
        self.ui_snapshot = self.root / "ui_headers.json"
        self.ui_snapshot.write_text(json.dumps({str(header): reuse.sha(header)}))
        for index in range(2):
            (self.build / f"obj_{index}.o").write_bytes(f"object fingerprint {index}".encode())
        self.capture()

    def tearDown(self):
        self.temporary.cleanup()

    def snapshot(self, name):
        path = self.root / name
        path.write_text(json.dumps({key: hashlib.sha256((self.program / key).read_bytes()).hexdigest()
                                    for key in self.names}))
        return path

    def invoke(self, action, snapshot, allowed=None, flags=None, sources=None, build_type="Release"):
        arguments = ["arm64_object_reuse.py", action, str(self.manifest), str(snapshot), str(self.program),
                     str(self.build), str(self.compiler), build_type, "--flags"]
        arguments += self.flags if flags is None else flags
        arguments += ["--sources"] + (self.sources if sources is None else sources)
        environment = {} if allowed is None else {"SN100_ARM64_REUSE_ALLOWED_CPP": allowed}
        if allowed is not None:
            environment["SN100_ARM64_REUSE_UI_SNAPSHOT"] = str(self.ui_snapshot)
        output = io.StringIO()
        with mock.patch.object(sys, "argv", arguments), mock.patch.dict(os.environ, environment, clear=True):
            with redirect_stdout(output):
                reuse.main()
        return output.getvalue().splitlines()

    def capture(self):
        snapshot = self.snapshot("original_snapshot.json")
        # Synthetic fresh-object timestamps exercise the capture guard only.
        stamp = snapshot.stat().st_mtime_ns + 1000000000
        for index in range(2):
            os.utime(self.build / f"obj_{index}.o", ns=(stamp, stamp))
        self.invoke("capture", snapshot)

    def candidates(self, allowed=None, **options):
        return self.invoke("reusable", self.snapshot("candidate_snapshot.json"), allowed, **options)

    def change(self, name):
        with (self.program / name).open("a") as target:
            target.write("// changed fingerprint\n")

    def test_unchanged_objects_are_reusable(self):
        self.assertEqual(self.candidates(), ["0", "1"])

    def test_default_rejects_cpp_change(self):
        self.change("samples/sample_page.cpp")
        with self.assertRaisesRegex(ValueError, "contract changed"):
            self.candidates()

    def test_explicit_cpp_change_recompiles_only_that_index(self):
        self.change("samples/sample_page.cpp")
        self.assertEqual(self.candidates("samples/sample_page.cpp"), ["1"])

    def test_header_form_qrc_resource_and_fixture_changes_are_rejected(self):
        for name in ("stable.h", "form.ui", "resources.qrc", "resources/icon.svg", "tests/fixtures/example.json"):
            with self.subTest(name=name):
                before = (self.program / name).read_bytes()
                self.change(name)
                with self.assertRaisesRegex(ValueError, "Unlisted compilation inputs"):
                    self.candidates("samples/sample_page.cpp")
                (self.program / name).write_bytes(before)

    def test_unlisted_cpp_and_added_input_are_rejected(self):
        self.change("stable.cpp")
        with self.assertRaisesRegex(ValueError, "Unlisted compilation inputs"):
            self.candidates("samples/sample_page.cpp")
        self.names.append("new.cpp")
        (self.program / "new.cpp").write_text("// new input\n")
        with self.assertRaisesRegex(ValueError, "input keys changed"):
            self.candidates("samples/sample_page.cpp")

    def test_flags_compiler_order_build_type_and_build_script_are_pinned(self):
        for options in ({"flags": ["-O0"]}, {"sources": self.sources[::-1]}, {"build_type": "Debug"}):
            with self.subTest(options=options), self.assertRaisesRegex(ValueError, "contract changed"):
                self.candidates("samples/sample_page.cpp", **options)
        for path in (self.compiler, self.program / "shell/build.sh"):
            before = path.read_bytes()
            path.write_bytes(before + b"changed\n")
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "contract changed"):
                self.candidates("samples/sample_page.cpp")
            path.write_bytes(before)

    def test_tampered_object_is_not_reused(self):
        (self.build / "obj_1.o").write_bytes(b"tampered object fingerprint")
        self.assertEqual(self.candidates(), ["0"])

    def test_changed_generated_ui_header_rejects_reuse(self):
        (self.build / "ui_fixture.h").write_text("// changed generated header\n")
        with self.assertRaisesRegex(ValueError, "Generated UI headers changed"):
            self.candidates("samples/sample_page.cpp")

    def test_missing_generated_ui_snapshot_rejects_reuse(self):
        self.ui_snapshot.unlink()
        with self.assertRaises(FileNotFoundError):
            self.candidates("samples/sample_page.cpp")

    def test_old_snapshot_cannot_hide_changed_cpp(self):
        self.change("samples/sample_page.cpp")
        with self.assertRaisesRegex(ValueError, "Frozen compilation input changed"):
            self.invoke("reusable", self.root / "original_snapshot.json", "samples/sample_page.cpp")

    def test_included_cpp_is_rejected(self):
        (self.program / "stable.h").write_text('#include "samples/sample_page.cpp"\n')
        self.capture()
        self.change("samples/sample_page.cpp")
        with self.assertRaisesRegex(ValueError, "included by another"):
            self.candidates("samples/sample_page.cpp")

    def test_computed_include_is_not_assumed_independent(self):
        (self.program / "stable.cpp").write_text('#define IMPL "samples/sample_page.cpp"\n#include IMPL\n')
        self.capture()
        self.change("samples/sample_page.cpp")
        with self.assertRaisesRegex(ValueError, "computed or ambiguous"):
            self.candidates("samples/sample_page.cpp")

    def test_allowance_is_one_normalized_cpp_in_sources(self):
        for name in ("../samples/sample_page.cpp", "./samples/sample_page.cpp", "stable.h",
                     "samples/sample_page.cpp,stable.cpp", str(self.program / "samples/sample_page.cpp")):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "normalized frozen relative"):
                self.candidates(name)
        self.names.append("unused.cpp")
        (self.program / "unused.cpp").write_text("// not in SOURCES\n")
        self.capture()
        with self.assertRaisesRegex(ValueError, "exactly once in SOURCES"):
            self.candidates("unused.cpp")


if __name__ == "__main__":
    unittest.main()
