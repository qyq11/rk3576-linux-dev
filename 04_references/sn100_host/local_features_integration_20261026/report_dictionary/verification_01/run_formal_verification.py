#!/usr/bin/env python3
"""Fresh formal SQL and dictionary GUI verification under the assigned serial slot."""

import argparse
import hashlib
import json
import os
import re
import shlex
from pathlib import Path
import platform
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


PREPARATION = Path(__file__).resolve().parent
REPOSITORY = Path(__file__).resolve().parents[4]
DEPENDENCIES = PREPARATION / "source_dependencies.json"
QMAKE = "/usr/lib/qt5/bin/qmake"
MAKE = "/usr/bin/make"


class StopRequested(Exception):
    pass


def request_stop(signum, _frame):
    raise StopRequested("Controller received signal " + str(signum))


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def relative_source(value):
    path = (REPOSITORY / value).resolve()
    if not path.is_relative_to(REPOSITORY):
        raise ValueError("Input is outside the repository: " + value)
    return path


def verify_inputs(manifest):
    actual = {}
    for item in manifest["inputs"]:
        path = relative_source(item["path"])
        actual[item["path"]] = sha256(path)
        if actual[item["path"]] != item["sha256"]:
            raise ValueError("Frozen input hash changed: " + item["path"])
    return actual


def tool_snapshot(manifest):
    actual = {}
    requested = dict(manifest["tools"])
    requested["python"] = sys.executable
    for name, value in requested.items():
        path = Path(value).resolve(strict=True)
        actual[name] = {"requested": value, "resolved": str(path),
                        "bytes": path.stat().st_size, "sha256": sha256(path)}
    return actual


def save_receipt(path, data):
    temporary = path.with_suffix(".writing.json")
    temporary.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def group_exists(pgid):
    try:
        os.killpg(pgid, 0)
        return True
    except ProcessLookupError:
        return False


def reclaim_group(pgid):
    actions = []
    for kind in (signal.SIGTERM, signal.SIGKILL):
        if not group_exists(pgid):
            break
        try:
            os.killpg(pgid, kind)
            actions.append(kind.name)
        except ProcessLookupError:
            break
        until = time.monotonic() + 2.0
        while group_exists(pgid) and time.monotonic() < until:
            time.sleep(0.05)
    return {"signals": actions, "remaining": group_exists(pgid)}


def run_step(argv, cwd, name, receipt, receipt_path, environment, deadline, limit):
    remaining = min(limit, deadline - time.monotonic())
    if remaining <= 0:
        raise TimeoutError("Overall 300-second execution budget expired")
    log = receipt_path.parent / (name + ".log")
    step = {"name": name, "argv": [str(value) for value in argv], "cwd": str(cwd),
            "log": str(log), "timeout_seconds": remaining, "exit_code": None,
            "timed_out": False}
    receipt["steps"].append(step)
    save_receipt(receipt_path, receipt)
    started = time.monotonic()
    with log.open("wb") as output:
        process = subprocess.Popen(step["argv"], cwd=cwd, env=environment,
                                   stdin=subprocess.DEVNULL, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        step["pid"] = process.pid
        step["pgid"] = process.pid
        interruption = None
        try:
            process.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            step["timed_out"] = True
        except BaseException as error:
            interruption = error
            step["interruption"] = type(error).__name__ + ": " + str(error)
        finally:
            # Reap the parent before checking the group so a zombie is not
            # mistaken for a surviving compiler/test. Finish owned cleanup on interrupt.
            previous_term = signal.signal(signal.SIGTERM, signal.SIG_IGN)
            previous_int = signal.signal(signal.SIGINT, signal.SIG_IGN)
            try:
                if process.poll() is None:
                    try:
                        os.killpg(process.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    try:
                        process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        try:
                            os.killpg(process.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        process.wait(timeout=3)
                step["cleanup"] = reclaim_group(process.pid)
                step["exit_code"] = process.poll()
                step["elapsed_seconds"] = time.monotonic() - started
                save_receipt(receipt_path, receipt)
            finally:
                signal.signal(signal.SIGTERM, previous_term)
                signal.signal(signal.SIGINT, previous_int)
    if interruption is not None:
        raise interruption
    if step["timed_out"] or step["exit_code"] != 0 or step["cleanup"]["remaining"]:
        raise RuntimeError("Step failed or left a process group: " + name)


def inspect_qtest(xml_path, target):
    document = ET.parse(xml_path).getroot()
    functions = [item.attrib["name"] for item in document.findall("TestFunction")]
    if sorted(functions) != sorted(target["xml_functions"]):
        raise ValueError("Unexpected/incomplete QtTest function set: " + str(functions))
    incidents = document.findall(".//Incident")
    passes = sum(item.attrib.get("type") == "pass" for item in incidents)
    failures = [item.attrib for item in incidents if item.attrib.get("type") != "pass"]
    skipped = [item.attrib for item in document.findall(".//Message")
               if item.attrib.get("type") == "skip"]
    if failures or skipped or passes != target["expected_pass_incidents"]:
        raise ValueError("QtTest incomplete/failure/skip: " + str(
            {"passes": passes, "failures": failures, "skipped": skipped}))
    return {"pass_incidents": passes, "failures": failures, "skipped": skipped,
            "functions": functions, "xml_sha256": sha256(xml_path)}


FORMAL = REPOSITORY / "06_programs/sn100_host"
TARGETS = [
    {"name": "report_dictionary_store_contract_test", "modes": ["core"],
     "expected_pass_incidents": 10, "xml_functions": ["initTestCase", "filterLengthBoundaries",
       "fiveTypesNameMnemonicLikeAndReadOnly", "failuresAlwaysCorrelatedAndDoNotFinishEditorRequests", "cleanupTestCase"]},
    {"name": "report_dictionary_dialog_test", "modes": ["offscreen", "xcb"],
     "expected_pass_incidents": 9, "xml_functions": ["initTestCase", "originalInnerGeometryAndNameOnly",
       "staleRequestTypeAndFilterCannotReplaceCurrentRows", "filterLimitRejectsWholeInputAndDoesNotLimitSelectedName",
       "errorIsNotEmptyAndRetryUsesFreshRequest", "closeWithoutSelection", "cleanupTestCase"]},
    {"name": "report_dictionary_page_test", "modes": ["offscreen", "xcb"],
     "expected_pass_incidents": 6, "xml_functions": ["initTestCase", "fiveDraftFieldsOnlyOriginalSaveCommits",
       "failuresEmptyAndUnrepresentableNamesPreserveExistingDraft",
       "cancelledFlightCoalescesAndCannotFillReopenedField", "cleanupTestCase"]},
]
TOOLS = {"qmake": QMAKE, "make": MAKE, "cxx": "/usr/bin/g++", "cc": "/usr/bin/gcc",
         "moc": "/usr/lib/qt5/bin/moc", "uic": "/usr/lib/qt5/bin/uic", "rcc": "/usr/lib/qt5/bin/rcc"}


def collect_target(name):
    files, includes, generated = {}, {FORMAL, FORMAL / "history", FORMAL / "tests"}, set()

    def add(path, role):
        path = path.resolve()
        if not path.is_relative_to(FORMAL) or not path.is_file():
            raise ValueError("Missing/non-formal recursive dependency: " + str(path))
        key = str(path.relative_to(REPOSITORY))
        if key in files:
            return
        files[key] = {"path": key, "role": role, "bytes": path.stat().st_size, "sha256": sha256(path)}
        suffix = path.suffix.lower()
        if suffix in (".pro", ".pri"):
            text = re.sub(r"\\\s*\n", " ", path.read_text(encoding="utf-8"))
            for raw in text.splitlines():
                line = raw.split("#", 1)[0].strip()
                match = re.fullmatch(r"include\(([^)]+)\)", line)
                if match:
                    value = match.group(1).replace("$$PWD", str(path.parent))
                    add(path.parent / value, "recursive-qmake-include")
                match = re.match(r"(SOURCES|HEADERS|FORMS|RESOURCES|DISTFILES|INCLUDEPATH)\s*\+?=\s*(.*)", line)
                if match:
                    for value in shlex.split(match.group(2)):
                        value = value.replace("$$PWD", str(path.parent))
                        if "$" in value:
                            raise ValueError("Unresolved qmake expression: " + value)
                        selected = (path.parent / value).resolve()
                        if match.group(1) == "INCLUDEPATH":
                            includes.add(selected)
                        else:
                            add(selected, match.group(1))
        elif suffix == ".qrc":
            for resource in ET.parse(path).findall(".//file"):
                add(path.parent / resource.text, "qrc-resource-bytes")
        elif suffix == ".ui":
            document = ET.parse(path)
            for resource in document.findall(".//resources/include"):
                add(path.parent / resource.attrib["location"], "ui-resource-reference")
            for header in document.findall(".//customwidget/header"):
                resolve_include(path, header.text)
        elif suffix in (".cpp", ".h"):
            for value in re.findall(r'^\s*#\s*include\s*"([^\"]+)"', path.read_text(encoding="utf-8"), re.M):
                resolve_include(path, value)

    def resolve_include(owner, value):
        if value.endswith(".moc") or (value.startswith("ui_") and value.endswith(".h")):
            generated.add(value)
            return
        for directory in [owner.parent] + sorted(includes):
            candidate = directory / value
            if candidate.is_file():
                add(candidate, "recursive-quoted-include")
                return
        raise ValueError("Unresolved project include in " + str(owner) + ": " + value)

    add(FORMAL / "tests" / (name + ".pro"), "formal-test-project")
    add(FORMAL / "history/report_dictionary_origins.json", "formal-source-provenance")
    return {"files": sorted(files.values(), key=lambda item: item["path"]),
            "generated_includes": sorted(generated)}


def collect_scope():
    per_target = {target["name"]: collect_target(target["name"]) for target in TARGETS}
    union = {}
    for scope in per_target.values():
        union.update({item["path"]: item for item in scope["files"]})
    return {"schema": "sn100-formal-dictionary-target-recursive-inputs-v1", "scope": "new-formal-targets-only-not447",
            "targets": per_target, "inputs": sorted(union.values(), key=lambda item: item["path"]),
            "tools": TOOLS, "expected_qt_version": "5.15.13"}


def inspect_build(build, scope):
    bound = {str(relative_source(item["path"])) for item in scope["files"]}
    actual_project = set()
    text = (build / "Makefile").read_text(encoding="utf-8")
    for token in re.split(r"\s+", text.replace("\\\n", " ")):
        token = token.strip("'\":\\")
        if "$" in token or not re.search(r"\.(?:cpp|h|ui|qrc|pri|pro|json|png)$", token):
            continue
        path = (build / token).resolve()
        if path.is_file() and path.is_relative_to(FORMAL):
            actual_project.add(str(path))
    unbound = actual_project - bound
    if unbound:
        raise ValueError("Generated Makefile uses unbound formal files: " + str(sorted(unbound)))
    artifacts = []
    for path in sorted(build.rglob("*")):
        if path.is_file() and path.suffix in (".o", ".h", ".cpp"):
            artifacts.append({"path": str(path.relative_to(build)), "bytes": path.stat().st_size, "sha256": sha256(path)})
    return {"actual_formal_makefile_files": sorted(actual_project), "unbound": [],
            "fresh_build_directory": True, "generated_sources_headers_objects": artifacts,
            "object_count": sum(item["path"].endswith(".o") for item in artifacts)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--slot-token", required=True)
    parser.add_argument("--attempt", required=True)
    args = parser.parse_args()
    if sys.platform != "linux" or platform.machine() != "x86_64":
        parser.error("Native WSL x86_64 required")
    if not re.fullmatch(r"attempt_[0-9]{2}", args.attempt):
        parser.error("A new attempt_XX directory name is required")
    output = PREPARATION / args.attempt
    output.mkdir(exist_ok=False)
    receipt_path = output / "receipt.json"
    scope = collect_scope()
    scope_path = output / "formal_inputs.json"
    scope_path.write_text(json.dumps(scope, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    receipt = {"schema": "sn100-formal-dictionary-execution-v1", "status": "started", "slot_token": args.slot_token,
               "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "steps": [], "targets": [],
               "controller_sha256": sha256(Path(__file__)), "input_manifest_sha256": sha256(scope_path),
               "source_scope": "formal recursive target closure, not inherited447/private executable or object", "jobs": 2,
               "child_budget_seconds": 300, "source_count": len(scope["inputs"])}
    signal.signal(signal.SIGTERM, request_stop)
    signal.signal(signal.SIGINT, request_stop)
    deadline = time.monotonic() + 300
    environment = os.environ.copy()
    for key in ("QMAKESPEC", "QMAKEPATH", "QMAKEFEATURES", "QT_PLUGIN_PATH", "QML2_IMPORT_PATH", "MAKEFLAGS", "MFLAGS", "QT_FATAL_WARNINGS"):
        environment.pop(key, None)
    receipt["display_environment"] = {key: environment.get(key) for key in
        ("DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "LD_LIBRARY_PATH", "QT_SCALE_FACTOR")}
    final_code = 1
    save_receipt(receipt_path, receipt)
    try:
        receipt["inputs_before"] = verify_inputs(scope)
        receipt["tools_before"] = tool_snapshot(scope)
        run_step([QMAKE, "-query"], output, "qmake_query", receipt, receipt_path, environment, deadline, 20)
        if "QT_VERSION:5.15.13" not in (output / "qmake_query.log").read_text().splitlines():
            raise ValueError("Unexpected native Qt version")
        run_step([MAKE, "--version"], output, "make_version", receipt, receipt_path, environment, deadline, 20)
        run_step(["/usr/bin/g++", "--version"], output, "compiler_version", receipt, receipt_path, environment, deadline, 20)
        run_step(["/usr/bin/g++", "-dumpmachine"], output, "compiler_target", receipt, receipt_path, environment, deadline, 20)
        if not (output / "compiler_target.log").read_text().strip().startswith("x86_64-"):
            raise ValueError("Unexpected compiler architecture")
        for target in TARGETS:
            name = target["name"]
            build = output / name
            build.mkdir()
            project = FORMAL / "tests" / (name + ".pro")
            verify_inputs(scope)
            run_step([QMAKE, str(project), "-spec", "linux-g++", "CONFIG+=release", "CONFIG-=debug",
                      "QMAKE_CXX=/usr/bin/g++", "QMAKE_LINK=/usr/bin/g++"], build, name + "_qmake",
                     receipt, receipt_path, environment, deadline, 45)
            run_step([MAKE, "-j2"], build, name + "_make", receipt, receipt_path, environment, deadline, 180)
            executable = build / name
            elf = executable.read_bytes()[:20]
            if elf[:5] != b"\x7fELF\x02" or int.from_bytes(elf[18:20], "little") != 62:
                raise ValueError("Fresh test executable is not ELF64 x86_64")
            result = {"name": name, "binary_sha256": sha256(executable), "build": inspect_build(build, scope["targets"][name]), "runs": []}
            receipt["targets"].append(result)
            save_receipt(receipt_path, receipt)
            for mode in target["modes"]:
                runtime = output / "runtime" / name / mode
                runtime.mkdir(parents=True)
                selected = environment.copy()
                selected["TMPDIR"] = str(runtime)
                selected["XDG_CACHE_HOME"] = str(runtime / "cache")
                if mode == "core":
                    selected["QT_FATAL_WARNINGS"] = "1"
                else:
                    selected["QT_QPA_PLATFORM"] = mode
                xml_path = build / ("result_" + mode + ".xml")
                run_step([str(executable), "-o", str(xml_path) + ",xml"], build, name + "_" + mode,
                         receipt, receipt_path, selected, deadline, 60)
                run = {"mode": mode, "runtime": str(runtime), "actual": inspect_qtest(xml_path, target)}
                result["runs"].append(run)
                save_receipt(receipt_path, receipt)
                verify_inputs(scope)
        receipt["status"] = "passed"
        final_code = 0
    except (StopRequested, KeyboardInterrupt) as error:
        receipt["status"] = "interrupted"
        receipt["error"] = type(error).__name__ + ": " + str(error)
        final_code = 130
    except Exception as error:
        receipt["status"] = "failed"
        receipt["error"] = type(error).__name__ + ": " + str(error)
    finally:
        try:
            receipt["inputs_after"] = verify_inputs(scope)
            receipt["tools_after"] = tool_snapshot(scope)
            if collect_scope() != scope or receipt["tools_after"] != receipt["tools_before"]:
                raise ValueError("Recursive source closure or bound tools changed")
            receipt["controller_sha256_after"] = sha256(Path(__file__))
            if receipt["controller_sha256_after"] != receipt["controller_sha256"]:
                raise ValueError("Controller changed during execution")
        except Exception as error:
            receipt["status"] = "failed-input-changed"
            receipt["input_error"] = type(error).__name__ + ": " + str(error)
            final_code = 1
        receipt["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        save_receipt(receipt_path, receipt)
    print(json.dumps({"status": receipt["status"], "receipt": str(receipt_path), "sources": len(scope["inputs"])}, ensure_ascii=False))
    return final_code


if __name__ == "__main__":
    raise SystemExit(main())


