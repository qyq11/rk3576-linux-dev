#!/usr/bin/env python3
"""Serial, calibration-only offline acceptance; no RK/device/network or resource run."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[4]
MODULE = ROOT / "06_programs/sn100_host/calibration"
TEMP = ROOT / "temp/sn100_host/calibration_local_20261025"
EVIDENCE = ROOT / "04_references/sn100_host/calibration_local_20261025"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs():
    parent = MODULE.parent
    files = list(MODULE.rglob("*")) + list((parent / "shell/widgets").rglob("*"))
    files += [parent / name for name in ["data/account_store.h", "projects/projects_store.h",
        "projects/project_advanced.h", "projects/projects_widgets.pri", "reagents/reagent_types.h"]]
    return {str(path.relative_to(ROOT)): {"sha256": digest(path), "bytes": path.stat().st_size}
        for path in sorted(set(files)) if path.is_file() and path.suffix in
        {".h", ".cpp", ".ui", ".pri", ".pro", ".sh", ".py"}}


def main():
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    TEMP.mkdir(parents=True, exist_ok=True)
    receipts = []
    before = inputs()
    results = {"schema": "sn100-calibration-independent-verification-v1", "started_utc":
        datetime.now(timezone.utc).isoformat(), "receipts": receipts, "scope":
        "WSL x86_64 Qt5 Release, anonymous local DB; no Windows/ARM/RK/device efficacy/resource qualification"}

    def save():
        (EVIDENCE / "verification.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")

    def run(name, command, env=None, expected=0):
        target = EVIDENCE / (name + ".log")
        settings = os.environ.copy()
        if env: settings.update(env)
        started = time.monotonic()
        with target.open("wb") as output:
            process = subprocess.run(command, cwd=ROOT, env=settings, stdout=output, stderr=subprocess.STDOUT, timeout=240)
        text = target.read_text(encoding="utf-8", errors="replace")
        receipt = {"name": name, "command": command, "environment_overrides": env or {},
            "exit_code": process.returncode, "expected_exit_code": expected,
            "elapsed_seconds": round(time.monotonic() - started, 6),
            "log": str(target.relative_to(ROOT)), "log_sha256": digest(target)}
        totals = re.findall(r"Totals: (\d+) passed, (\d+) failed, (\d+) skipped", text)
        if totals: receipt["qt_test_totals"] = dict(zip(["passed", "failed", "skipped"], map(int, totals[-1])))
        receipts.append(receipt); save()
        print(json.dumps(receipt, ensure_ascii=False), flush=True)
        if process.returncode != expected:
            raise RuntimeError(f"{name}: exit {process.returncode}, expected {expected}; see {target}")

    try:
        run("environment", ["bash", "-lc", "uname -a && /usr/lib/qt5/bin/qmake -v && g++ --version && ldd --version"])
        for mode, directory in [("store-test", "wsl_store_tests"), ("page-test", "wsl_page_tests"), ("build", "wsl_release")]:
            build = TEMP / directory
            if (build / "Makefile").is_file(): run("clean_" + directory, ["make", "-C", str(build), "clean"])
            env = {"QT_QPA_PLATFORM": "offscreen", "CALIBRATION_TEST_ROOT": str(TEMP / "runtime/final_offscreen")}
            if mode == "page-test": env["CALIBRATION_SCREENSHOT_DIR"] = str(EVIDENCE / "screenshots/offscreen")
            run("final_" + mode, ["bash", str(MODULE / "wsl.sh"), mode], env)
        run("final_page_xcb", [str(TEMP / "wsl_page_tests/calibration_page_test")],
            {"QT_QPA_PLATFORM": "xcb", "CALIBRATION_TEST_ROOT": str(TEMP / "runtime/final_xcb"),
             "CALIBRATION_SCREENSHOT_DIR": str(EVIDENCE / "screenshots/xcb")})
        run_id = os.environ.get("CALIBRATION_VERIFY_RUN", time.strftime("%Y%m%d_%H%M%S", time.gmtime()))
        catalog = TEMP / "runtime/final_standalone" / run_id / "catalog.db"
        run("create_anonymous_catalog", ["python3", str(MODULE / "tests/calibration_create_catalog.py"), str(catalog)])
        binary = TEMP / "wsl_release/sn100_calibration"
        for qpa in ["offscreen", "xcb"]:
            run("standalone_" + qpa, [str(binary), "--data-dir", str(catalog.parent / qpa),
                "--catalog-db", str(catalog), "--exit-after-ms", "1200"], {"QT_QPA_PLATFORM": qpa})
        run("invalid_cli", [str(binary)], {"QT_QPA_PLATFORM": "offscreen"}, expected=2)
        after = inputs()
        results["source_inputs_unchanged_during_verification"] = before == after
        (EVIDENCE / "source_inputs.json").write_text(json.dumps(after, ensure_ascii=False, indent=2), encoding="utf-8")
        results["artifacts"] = {str(path.relative_to(ROOT)): {"sha256": digest(path), "bytes": path.stat().st_size}
            for path in [binary, TEMP / "wsl_store_tests/calibration_store_test", TEMP / "wsl_page_tests/calibration_page_test"]}
        original = ROOT / "06_programs/sn100_host_reference/Seerain450_PC/Seerain450_PC/frmcalibration/frmcalibrationmaterial.ui"
        results["ui_byte_identical_to_reference"] = digest(original) == digest(MODULE / "frmcalibrationmaterial.ui")
        results["passed"] = before == after and results["ui_byte_identical_to_reference"]
        results["finished_utc"] = datetime.now(timezone.utc).isoformat(); save()
        return 0 if results["passed"] else 1
    except Exception as error:
        results["passed"] = False; results["failure"] = str(error); save(); raise


if __name__ == "__main__":
    raise SystemExit(main())
