#!/usr/bin/env python3
"""Explicitly resume a failed, source-frozen ARM build without trusting mtimes.

Capture is used only after a full compilation reached the link stage. Every
reused object is bound to that source order, exact flags, compiler and SHA-256.
Changed generated moc/uic/resource sources are compiled again.
"""
import hashlib
import json
from pathlib import Path
import shutil
import sys


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    action, manifest_name, snapshot_name, program_name, build_name, compiler_name, build_type, *arguments = sys.argv[1:]
    split = arguments.index("--sources")
    if not arguments or arguments[0] != "--flags":
        raise ValueError("Expected --flags followed by --sources")
    flags = arguments[1:split]
    sources = arguments[split + 1:]
    program, build = Path(program_name), Path(build_name)
    manifest = Path(manifest_name)
    compiler = Path(shutil.which(compiler_name)).resolve()
    compiler_hash = sha(compiler)
    inputs = json.loads(Path(snapshot_name).read_text(encoding="utf-8"))
    # The resumed fix may change host/build/lifecycle scripts. All compiled
    # business, headers, forms, resources, fixtures and qmake inputs must match.
    compiled_inputs = {name: digest for name, digest in inputs.items()
                       if Path(name).suffix not in (".sh", ".py")}
    for name, digest in compiled_inputs.items():
        if sha(program / name) != digest:
            raise ValueError("Frozen compilation input changed: " + name)
    if action == "capture":
        snapshot_time = Path(snapshot_name).stat().st_mtime_ns
        objects = []
        for index, source in enumerate(sources):
            target = build / ("obj_%d.o" % index)
            if not target.is_file() or target.stat().st_mtime_ns <= snapshot_time:
                raise ValueError("No freshly compiled object for: " + source)
            objects.append({"source": source, "source_sha256": sha(source),
                            "object": str(target), "object_sha256": sha(target)})
        saved = {"schema": "sn100-explicit-arm-link-resume-v1", "compiled_inputs": compiled_inputs,
                 "compiler_sha256": compiler_hash, "build_type": build_type, "flags": flags,
                 "build_script_sha256": sha(program / "shell/build.sh"), "objects": objects}
        manifest.write_text(json.dumps(saved, indent=2) + "\n", encoding="utf-8")
        print("Captured %d compiled objects" % len(objects))
        return
    if action != "reusable":
        raise ValueError("Expected capture or reusable")
    saved = json.loads(manifest.read_text(encoding="utf-8"))
    if (saved["compiled_inputs"] != compiled_inputs or saved["compiler_sha256"] != compiler_hash
            or saved["flags"] != flags or saved["build_type"] != build_type
            or saved["build_script_sha256"] != sha(program / "shell/build.sh")
            or [item["source"] for item in saved["objects"]] != sources):
        raise ValueError("Object reuse contract changed; perform a full build")
    for index, item in enumerate(saved["objects"]):
        target = build / ("obj_%d.o" % index)
        if (str(target) == item["object"] and target.is_file()
                and sha(target) == item["object_sha256"] and sha(sources[index]) == item["source_sha256"]):
            print(index)


if __name__ == "__main__":
    main()
