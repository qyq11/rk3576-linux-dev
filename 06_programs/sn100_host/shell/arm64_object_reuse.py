#!/usr/bin/env python3
"""Explicitly resume a failed, source-frozen ARM build without trusting mtimes.

Capture is used only after a full compilation reached the link stage. Every
reused object is bound to that source order, exact flags, compiler and SHA-256.
Changed generated moc/uic/resource sources are compiled again.
SN100_ARM64_REUSE_ALLOWED_CPP may explicitly permit one independent .cpp to
change. A changed object's fingerprint prevents reuse, while all other
compilation inputs and a separate generated-UI header snapshot stay fixed.
"""
import hashlib
from functools import lru_cache
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import sys


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_independent_cpp(name, program, build, inputs, sources, flags):
    @lru_cache(maxsize=None)
    def resolved(path):
        return path.resolve()

    @lru_cache(maxsize=None)
    def is_file(path):
        return path.is_file()

    relative = PurePosixPath(name)
    if (relative.is_absolute() or relative.suffix != ".cpp" or ".." in relative.parts
            or relative.as_posix() != name or "\\" in name or name not in inputs):
        raise ValueError("Allowed source must name one normalized frozen relative .cpp")
    target = (program / name).resolve(strict=True)
    if not target.is_relative_to(program.resolve()):
        raise ValueError("Allowed source escapes the program")
    if sum(resolved(Path(source)) == target for source in sources) != 1:
        raise ValueError("Allowed .cpp must occur exactly once in SOURCES")
    directories = []
    for index, flag in enumerate(flags):
        if flag in ("-I", "-isystem", "-iquote"):
            directories.append(Path(flags[index + 1]))
        elif flag.startswith("-I"):
            directories.append(Path(flag[2:]))
    text_suffixes = {".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hh", ".hxx", ".inc"}
    scan = {program / path for path in inputs if Path(path).suffix in text_suffixes}
    scan.update(Path(path) for path in sources)
    scan.update(build.glob("ui_*.h"))
    # Search even commented directives conservatively. Do not discard arbitrary
    # comments globally: comment markers can also occur inside C++ strings.
    includes = re.compile(r"#(?:[ \t]|/\*.*?\*/)*include\b(?:[ \t]|/\*.*?\*/)*([^\n]*)", re.S)
    for path in sorted(scan):
        if resolved(path) == target:
            continue
        text = re.sub(r"\\\r?\n", "", path.read_text(encoding="utf-8", errors="replace"))
        for directive in includes.finditer(text):
            operand = directive.group(1).strip()
            literal = re.fullmatch(r'(?:"([^"\n]+)"|<([^>\n]+)>)\s*(?://[^\n]*)?', operand)
            if not literal:
                raise ValueError("Cannot verify computed or ambiguous #include in: " + str(path))
            included = literal.group(1) or literal.group(2)
            if PurePosixPath(included).name == target.name:
                raise ValueError("Allowed .cpp is included by another source/header: " + str(path))
            candidates = ([path.parent / included] if literal.group(1) else [])
            candidates += [directory / included for directory in directories]
            for candidate in candidates:
                if not is_file(candidate):
                    continue
                if resolved(candidate) == target:
                    raise ValueError("Allowed .cpp is included by another source/header: " + str(path))
                # The preprocessor uses the first matching search directory.
                break


def verify_generated_ui_headers(build):
    name = os.environ.get("SN100_ARM64_REUSE_UI_SNAPSHOT", "")
    if not name or not Path(name).is_absolute():
        raise ValueError("Explicit cpp reuse requires an absolute generated-UI header snapshot")
    saved = json.loads(Path(name).read_text(encoding="utf-8"))
    actual = {str(path): sha(path) for path in sorted(build.glob("ui_*.h"))}
    if not actual or saved != actual:
        raise ValueError("Generated UI headers changed; perform a full build")


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
    if (saved["compiler_sha256"] != compiler_hash
            or saved["flags"] != flags or saved["build_type"] != build_type
            or saved["build_script_sha256"] != sha(program / "shell/build.sh")
            or [item["source"] for item in saved["objects"]] != sources):
        raise ValueError("Object reuse contract changed; perform a full build")
    allowed = os.environ.get("SN100_ARM64_REUSE_ALLOWED_CPP", "")
    if not allowed:
        if saved["compiled_inputs"] != compiled_inputs:
            raise ValueError("Object reuse contract changed; perform a full build")
    else:
        if saved["compiled_inputs"].keys() != compiled_inputs.keys():
            raise ValueError("Compilation input keys changed; perform a full build")
        changed = {name for name, digest in compiled_inputs.items() if saved["compiled_inputs"][name] != digest}
        if changed - {allowed}:
            raise ValueError("Unlisted compilation inputs changed; perform a full build")
        verify_generated_ui_headers(build)
        verify_independent_cpp(allowed, program, build, compiled_inputs, sources, flags)
    for index, item in enumerate(saved["objects"]):
        target = build / ("obj_%d.o" % index)
        if (str(target) == item["object"] and target.is_file()
                and sha(target) == item["object_sha256"] and sha(sources[index]) == item["source_sha256"]):
            print(index)


if __name__ == "__main__":
    main()
