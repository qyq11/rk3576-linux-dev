"""WSL headless child-only renderer samples. No GUI, database or instrument.

Run only after the PDF authoring marker is recorded. The output directory must
be new. RSS/PSS peaks are observed samples, not an allocator's exact peak; VmHWM
is also recorded while the child exists. The sampler never changes priorities.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sample(pid):
    directory = Path("/proc") / str(pid)
    result = {"monotonic_s": time.monotonic()}
    try:
        fields = {}
        for line in (directory / "status").read_text().splitlines():
            key, _, value = line.partition(":")
            fields[key] = value.strip()
        for key in ("VmRSS", "VmHWM", "Threads"):
            result[key] = int(fields[key].split()[0]) if key in fields else None
        result["fd_count"] = len(list((directory / "fd").iterdir()))
        for line in (directory / "smaps_rollup").read_text().splitlines():
            if line.startswith("Pss:"):
                result["Pss_kib"] = int(line.split()[1])
                break
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        result["unavailable"] = True
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeat", type=int, default=5)
    parser.add_argument("--sample-ms", type=int, default=5)
    options = parser.parse_args()
    if not Path("/proc/self/status").exists() or os.name != "posix":
        raise RuntimeError("This sampler is for native WSL/Linux child processes")
    if not (1 <= options.repeat <= 20 and 1 <= options.sample_ms <= 100):
        raise ValueError("repeat must be 1..20 and sample-ms 1..100")
    options.output.mkdir(parents=True, exist_ok=False)
    results = {"renderer_sha256": sha(options.renderer), "snapshot_sha256": sha(options.snapshot),
               "sample_interval_ms": options.sample_ms,
               "scope": "only this child; no parent/interpreter resource included; RSS/PSS observed peaks",
               "runs": []}
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    for index in range(options.repeat):
        output = options.output / f"render_{index + 1:02d}.pdf"
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        start = time.monotonic()
        samples = []
        with (options.output / f"render_{index + 1:02d}.txt").open("wb") as log:
            process = subprocess.Popen([str(options.renderer.resolve()), str(options.snapshot.resolve()),
                                        str(output.resolve())], stdout=log, stderr=log, env=environment)
            timed_out = False
            while process.poll() is None:
                samples.append(sample(process.pid))
                if time.monotonic() - start >= 30:
                    timed_out = True
                    process.kill()
                    break
                time.sleep(options.sample_ms / 1000)
            exit_code = process.wait(timeout=5)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        elapsed = time.monotonic() - start
        peaks = {key: max((entry.get(key, 0) or 0 for entry in samples), default=0)
                 for key in ("VmRSS", "VmHWM", "Threads", "fd_count", "Pss_kib")}
        entry = {"run": index + 1, "elapsed_ms": elapsed * 1000, "exit_code": exit_code,
                 "timed_out": timed_out, "cpu_user_ms": (after.ru_utime - before.ru_utime) * 1000,
                 "cpu_system_ms": (after.ru_stime - before.ru_stime) * 1000,
                 "observed_peaks": peaks, "samples": samples,
                 "pdf_bytes": output.stat().st_size if output.exists() else 0,
                 "pdf_sha256": sha(output) if output.exists() else None}
        results["runs"].append(entry)
        if exit_code or timed_out:
            break
    results["passed"] = len(results["runs"]) == options.repeat and all(
        entry["exit_code"] == 0 and not entry["timed_out"] and entry["pdf_bytes"] > 1024
        for entry in results["runs"])
    (options.output / "measurement.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": results["passed"], "runs": [
        {key: value for key, value in entry.items() if key != "samples"} for entry in results["runs"]]}))
    return 0 if results["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
