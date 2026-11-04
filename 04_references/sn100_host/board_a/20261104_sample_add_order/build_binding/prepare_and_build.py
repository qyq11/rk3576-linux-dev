"""待Root审定的WSL构建封装；默认不编译，不运行产品、不上传、不连接板卡。"""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
PARENT = HERE.parent
REVIEW = PARENT / "root_sample_checker_board_a_review_01"
SNAPSHOT_SHA = "66bdc0ae1dd9d44988b6cbcf12fa80fe164a3a646909afd1b359467b63b99533"
CANONICAL_SOURCE_SHA = "f3519bacb0efca4077ef93ef21f59e906021585dbd9ae414a37e7bb9b2abf6d0"
CANONICAL_SOURCE = ROOT / "temp/sn100_host/workflow_next_20261103/integration_15_source_acceptance_01/sample_empty_backend/source_after.json"
CPP_SHA = "e6116eb485fcf272e5a5a86005d92eb6700006b492679619bd455cf5c40ad677"
BASE = ROOT / "07_backups/qt_environment/sn100_base_arm64"
TEST = ROOT / "07_backups/qt_environment/sn100_original_db_test_arm64"
DEV_DEB = BASE / "qtbase5-dev_5.15.13+dfsg-1ubuntu1_arm64.deb"
TEST_DEB = TEST / "packages/libqt5test5t64_5.15.13+dfsg-1ubuntu1_arm64.deb"
KNOWN_DEBS = {str(DEV_DEB): "ac55917652f7f4611a1915387a0655e33b52f51e30a08322e729d0b05dec49a7",
              str(TEST_DEB): "bb6a52002a984d7e9c73e5aa23dae2a11b20c311755a60bbe4f10ffb2847a4e2"}
HEADER_PREFIX = "usr/include/aarch64-linux-gnu/qt5/"
LIB_MEMBER = "usr/lib/aarch64-linux-gnu/libQt5Test.so.5.15.13"


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def regular(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink(), "需要普通文件，未恢复依赖或路径不符：" + str(path))
    return path


def digest(path):
    path = regular(path)
    before = path.stat()
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    after = path.stat()
    require((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
            == (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns), "读取时文件变化：" + str(path))
    return h.hexdigest()


def record(path):
    path = regular(path)
    return {"path": str(path), "sha256": digest(path), "bytes": path.stat().st_size}


def put(path, value):
    with Path(path).open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def put_text(path, value):
    with Path(path).open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(value)


def relative(name):
    require(isinstance(name, str) and name and "\\" not in name and ":" not in name,
            "不是项目内规范路径")
    value = PurePosixPath(name)
    require(not value.is_absolute() and all(p not in ("", ".", "..") for p in name.split("/")), "记录路径越界")
    return value


def check_record(item):
    path = ROOT / relative(item["path"])
    require(path.resolve() == path and path.is_relative_to(ROOT), "记录路径经过重定向")
    require(digest(path) == item["sha256"] and path.stat().st_size == item["bytes"], "记录不符：" + str(path))
    return path


def elf(path, allowed):
    with regular(path).open("rb") as stream:
        head = stream.read(64)
    require(len(head) == 64 and head[:6] == b"\x7fELF\x02\x01"
            and struct.unpack_from("<H", head, 18)[0] == 183
            and struct.unpack_from("<H", head, 16)[0] in allowed, "需要对应类型的ARM64小端ELF：" + str(path))


def frozen_files():
    manifest = REVIEW / "root_snapshot.json"
    require(digest(manifest) == SNAPSHOT_SHA, "Root第二版快照指纹不符")
    data = json.loads(manifest.read_text(encoding="utf-8"))
    require(len(data["records"]) == 9, "Root第二版应有九份原件")
    found = {}
    for entry in data["records"]:
        item = entry["copy"]
        path = check_record(item)
        require(path.parent == REVIEW and path.name not in found, "原件目录或名称不符")
        found[path.name] = record(path)
    require(found["sample_add_review.cpp"]["sha256"] == CPP_SHA, "不是第二版最终检查器")
    return found


@contextmanager
def full_build_lock(build):
    # 此锁只用于本封装的前核/后核，绝不跨越原build脚本调用。
    import fcntl
    path = regular(build / "arm64_build/build.lock")
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    try:
        require((os.fstat(fd).st_dev, os.fstat(fd).st_ino) == (path.stat().st_dev, path.stat().st_ino), "构建锁已替换")
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield
    finally:
        os.close(fd)


def inspect_build(build, release):
    require(regular(build / "build_exit.txt").read_text().strip() == "0", "第15批完整构建尚无实际0退出")
    snapshot_path = build / "source_snapshot.json"
    require(digest(CANONICAL_SOURCE) == CANONICAL_SOURCE_SHA, "第15批接纳的规范来源表指纹不符")
    sources = json.loads(CANONICAL_SOURCE.read_text(encoding="utf-8"))
    work_sources = json.loads(regular(snapshot_path).read_text(encoding="utf-8"))
    require(isinstance(sources, dict) and len(sources) == 697 and work_sources == sources,
            "本次完整构建快照的697份来源键值不等于接纳规范表；不以JSON表示的原始摘要互代")
    for name, value in sources.items():
        path = ROOT / "06_programs/sn100_host" / relative(name)
        require(path.resolve() == path and digest(path) == value, "正式源码已变化：" + name)
    binding_path = build / "object_binding.json"
    binding = json.loads(regular(binding_path).read_text(encoding="utf-8"))
    require(binding["mapping_basis"] == "reconstructed_from_frozen_build_rules_and_actual_tsv"
            and binding["source_files"] == sources and check_record(binding["source_snapshot"]) == snapshot_path,
            "完整对象来源绑定不符")
    # 原完整构建已生成的所有文件记录均回读，不把一段来源字符串当作完整核验。
    seen = set()
    def walk(value):
        if isinstance(value, dict):
            if {"path", "sha256", "bytes"}.issubset(value):
                key = (value["path"], value["sha256"], value["bytes"])
                if key not in seen:
                    check_record(value)
                    seen.add(key)
            for child in value.values():
                walk(child)
        elif isinstance(value, list):
            for child in value:
                walk(child)
    walk(binding)
    objects = binding["link_objects"]["main"]
    paths = [check_record(item) for item in objects]
    require(len(paths) == binding["object_counts"]["main"] and len(paths) == len(set(paths)), "主程序对象表不完整或重复")
    require(all(p.parent == build / "arm64_build" for p in paths), "对象不属于本次完整构建")
    for path in paths:
        elf(path, {1})
    mains = [x for x in binding["objects"] if x["source"]["path"].endswith("/shell/main.cpp") and "main" in x["link_roles"]]
    require(len(mains) == 1 and mains[0]["object"] in objects, "需要唯一的正式main对象")
    binary = check_record(binding["binaries"]["sn100_shell"])
    require(binary == build / "build/sn100_shell", "正式主程序不属于本次目录")
    elf(binary, {2, 3})
    latest = regular(build / "packages/latest_package")
    package = Path(latest.read_text().strip())
    require(package == build / "packages" / ("sn100_shell_" + release + ".tar.gz"), "包目录或发布号不符")
    with tarfile.open(regular(package), "r:gz") as bundle:
        ids = [x for x in bundle.getmembers() if x.name == "release_id"]
        bins = [x for x in bundle.getmembers() if x.name == "sn100_shell"]
        require(len(ids) == len(bins) == 1 and ids[0].isfile() and bins[0].isfile(), "包内关键成员不唯一或不是文件")
        require(bundle.extractfile(ids[0]).read().decode("ascii").strip() == release, "包内发布号不符")
        h = hashlib.sha256()
        with bundle.extractfile(bins[0]) as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                h.update(block)
        require(h.hexdigest() == digest(binary), "包内正式主程序不是本次完整构建")
    return {"release_id": release, "source_canonical": record(CANONICAL_SOURCE),
            "source_snapshot_raw": record(snapshot_path), "source_values_equal": True, "binding": record(binding_path),
            "formal_binary": record(binary), "latest_package": record(latest), "package": record(package),
            "object_count": len(paths), "object_records": objects}


def command(execution, stem, argv, environment, pass_fds=()):
    put(execution / (stem + ".command.json"), {"argv": [str(x) for x in argv], "cwd": str(ROOT)})
    with (execution / (stem + ".stdout.log")).open("xb") as out, (execution / (stem + ".stderr.log")).open("xb") as err:
        code = subprocess.run([str(x) for x in argv], cwd=ROOT, env=environment, stdout=out, stderr=err, pass_fds=pass_fds).returncode
    put_text(execution / (stem + ".exit"), str(code) + "\n")
    return code


def archive_record(path, sums):
    require(regular(sums).is_relative_to(ROOT / "07_backups/qt_environment"), "不是正式Qt恢复清单")
    matches = []
    wanted = path.relative_to(sums.parent).as_posix()
    for line in sums.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ?\*?(.+)", line)
        if match and match[2].removeprefix("./") == wanted:
            matches.append(match[1])
    require(len(matches) == 1 and digest(path) == matches[0] == KNOWN_DEBS[str(path)],
            "需要匹配正式清单的Qt依赖归档：" + str(path))
    return {"archive": record(path), "checksum_list": record(sums)}


def deb_members(execution, stem, archive, wanted, environment):
    argv = ["dpkg-deb", "--fsys-tarfile", str(archive)]
    put(execution / (stem + ".command.json"), {"argv": argv, "scope": "只流式读取归档成员，不解包、不安装；二进制stdout就是上述已固定deb的数据tar"})
    result = {}
    with (execution / (stem + ".stderr.log")).open("xb") as err:
        process = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=err, env=environment)
        try:
            with tarfile.open(fileobj=process.stdout, mode="r|") as bundle:
                for item in bundle:
                    name = item.name.removeprefix("./")
                    if name not in wanted:
                        continue
                    require(item.isfile() and name not in result, "依赖归档中目标不是唯一普通文件：" + name)
                    h = hashlib.sha256()
                    with bundle.extractfile(item) as stream:
                        for block in iter(lambda: stream.read(1024 * 1024), b""):
                            h.update(block)
                    result[name] = h.hexdigest()
            code = process.wait()
        except BaseException:
            process.kill()
            process.wait()
            raise
        finally:
            process.stdout.close()
    put_text(execution / (stem + ".exit"), str(code) + "\n")
    require(code == 0 and set(result) == set(wanted), "需要正式归档中全部QtTest头/库成员，未继续编译")
    return result


def dependencies(execution, stem, sysroot, environment):
    lib = sysroot / "usr/lib/aarch64-linux-gnu"
    qt = (sysroot / "usr/include/aarch64-linux-gnu/qt5").resolve(strict=True)
    require(qt.is_dir(), "需要匹配Qt5.15.13头文件恢复来源")
    fixed = regular(lib / "libQt5Test.so.5.15.13")
    link = lib / "libQt5Test.so"
    require(link.exists(), "需要qtbase5-dev的QtTest链接入口，不以其他库替代")
    target = link.resolve(strict=True)
    require(target.is_relative_to(lib.resolve()) and digest(target) == digest(fixed), "实际-lQt5Test目标与固定部署库不是同一字节")
    elf(fixed, {3})
    dev_origin = archive_record(DEV_DEB, BASE / "SHA256SUMS")
    test_origin = archive_record(TEST_DEB, TEST / "SHA256SUMS")
    versions = []
    for kind, package, expected_name in (("dev", DEV_DEB, "qtbase5-dev"), ("test", TEST_DEB, "libqt5test5t64")):
        code = command(execution, stem + "_" + kind + "_package", ["dpkg-deb", "-f", package, "Package", "Version", "Architecture"], environment)
        require(code == 0, "读取正式Qt依赖包元数据失败")
        text = (execution / (stem + "_" + kind + "_package.stdout.log")).read_text(encoding="utf-8")
        fields = dict(line.split(": ", 1) for line in text.splitlines() if ": " in line)
        require(fields.get("Package") == expected_name and fields.get("Version") == "5.15.13+dfsg-1ubuntu1"
                and fields.get("Architecture") == "arm64", "Qt依赖包版本/架构不符，不借旧库")
        versions.append(fields)
    headers = sorted(p for p in (qt / "QtTest").rglob("*") if p.is_file())
    require(headers and qt / "QtTest/qtestmouse.h" in headers and qt / "QtTest/QTest" in headers, "QtTest头不完整")
    headers += [regular(qt / "QtCore/qglobal.h"), regular(qt / "QtCore/qconfig.h")]
    header_records = []
    for path in headers:
        require(path.resolve().is_relative_to(qt) and not path.is_symlink(), "QtTest内部头存在越界或链接")
        header_records.append(record(path))
    wanted = {HEADER_PREFIX + p.relative_to(qt).as_posix() for p in headers}
    archived_headers = deb_members(execution, stem + "_dev_members", DEV_DEB, wanted, environment)
    for item in header_records:
        name = HEADER_PREFIX + Path(item["path"]).relative_to(qt).as_posix()
        require(item["sha256"] == archived_headers[name], "实际Qt头不等于正式恢复归档：" + name)
    archived_library = deb_members(execution, stem + "_test_members", TEST_DEB, {LIB_MEMBER}, environment)
    require(archived_library[LIB_MEMBER] == digest(fixed), "实际QtTest库不等于匹配正式ARM64归档")
    require(command(execution, stem + "_test_elf", ["aarch64-linux-gnu-readelf", "-h", "-d", fixed], environment) == 0, "QtTest ELF元数据读取失败")
    tools = {}
    for name in ("aarch64-linux-gnu-g++", "aarch64-linux-gnu-readelf", "dpkg-deb"):
        executable = shutil.which(name, path=environment["PATH"])
        require(executable, "缺少构建或元数据工具：" + name)
        tools[name] = record(Path(executable).resolve(strict=True))
    return {"sysroot": str(sysroot), "resolved_qt_headers": str(qt), "headers": header_records,
            "link_name": str(link), "link_target": record(target), "fixed_library": record(fixed),
            "package_metadata": versions, "dev_origin": dev_origin, "test_origin": test_origin, "metadata_tools": tools}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_directory")
    parser.add_argument("release")
    parser.add_argument("sysroot")
    parser.add_argument("--namespace", required=True)
    parser.add_argument("--execute", action="store_true", help="仅Root审定后显式设置；不设置时拒绝执行")
    args = parser.parse_args()
    require(args.execute, "候选默认不执行；须先由Root审核，再显式使用--execute")
    require(sys.platform == "linux" and os.geteuid() != 0 and "microsoft" in Path("/proc/sys/kernel/osrelease").read_text().lower(), "只允许普通WSL用户交叉编译")
    require(re.fullmatch(r"[0-9a-f]{64}", args.release), "发布号不是实际64位SHA")
    require(re.fullmatch(r"sample_checker15_board_a_exec_01_[a-z0-9_]+", args.namespace), "需要全新专项命名空间")
    build, sysroot = Path(args.build_directory), Path(args.sysroot)
    for path in (build, sysroot):
        require(path.is_absolute() and path.resolve() == path and path.is_dir() and path.is_relative_to(ROOT / "temp/sn100_host"), "参数必须为项目temp内无重定向绝对目录")
    require(build.parent == ROOT / "temp/sn100_host/workflow_next_20261103" and build.name.startswith("integration_15_"), "只接受Root批准的第15批完整目录")
    originals = frozen_files()
    execution = PARENT / args.namespace
    execution.mkdir()  # 不清理、不覆盖已有命名空间，包括失败目录。
    require(execution.resolve() == execution and execution.parents[3] == ROOT, "执行副本深度不符")
    put(execution / "root_intent.json", {"scope": "cross_build_and_file_metadata_only", "build": str(build),
        "release": args.release, "sysroot": str(sysroot), "wrapper": record(Path(__file__)),
        "root_snapshot_sha256": SNAPSHOT_SHA, "required_canonical_source_sha256": CANONICAL_SOURCE_SHA,
        "canonical_source_path": str(CANONICAL_SOURCE), "board_action": False})
    environment = dict(os.environ)
    for key in list(environment):
        if key.startswith("BASH_FUNC_"):
            environment.pop(key)
    for key in ("BASH_ENV", "ENV", "LD_PRELOAD", "LD_LIBRARY_PATH", "PYTHONPATH", "CC", "CXX", "CFLAGS", "CXXFLAGS", "LDFLAGS"):
        environment.pop(key, None)
    environment.update(PATH="/usr/local/bin:/usr/bin:/bin", LANG="C.UTF-8", LC_ALL="C.UTF-8", PYTHONDONTWRITEBYTECODE="1")
    child_code = None
    result = {"schema": "sn100-root-sample-checker15-build-wrapper-v1", "compiled": False, "board_operated": False,
              "runtime_verified": False, "formal_binary_replaced": False, "scope": "independent_checker_binary"}
    try:
        for name, item in originals.items():
            content = (REVIEW / name).read_bytes()
            require(hashlib.sha256(content).hexdigest() == item["sha256"], "复制时原件变化")
            with (execution / name).open("xb") as stream:
                stream.write(content)
            require(digest(execution / name) == item["sha256"], "执行副本回读失败")
        with (execution / "root_review_snapshot.captured.json").open("xb") as stream:
            stream.write((REVIEW / "root_snapshot.json").read_bytes())
        with full_build_lock(build):
            before = inspect_build(build, args.release)
        put(execution / "full_build.before.json", before)
        dep_before = dependencies(execution, "dependencies_before", sysroot, environment)
        put(execution / "dependencies.before.json", dep_before)
        for stem, argv in (("compiler_version", ["aarch64-linux-gnu-g++", "--version"]), ("compiler_target", ["aarch64-linux-gnu-g++", "-dumpmachine"])):
            require(command(execution, stem, argv, environment) == 0, "编译器元数据读取失败")
        require((execution / "compiler_target.stdout.log").read_text().strip() == "aarch64-linux-gnu", "不是ARM64交叉编译器")
        require(frozen_files() == originals, "Root专项原件在构建前变化")
        # 不持外层full_build_lock；原冻结脚本自己获取FD9锁，竞争时真实失败。
        import fcntl
        with (execution / "build.xtrace.log").open("xb") as trace:
            trace_fd = fcntl.fcntl(trace.fileno(), fcntl.F_DUPFD_CLOEXEC, 10)
            try:
                environment["BASH_XTRACEFD"] = str(trace_fd)
                environment["PS4"] = "+ ${BASH_SOURCE}:${LINENO}: "
                child_code = command(execution, "frozen_build", ["/bin/bash", "-x", execution / "build_sample_add_review.sh", build, args.release, sysroot], environment, (trace_fd,))
            finally:
                os.close(trace_fd)
                environment.pop("BASH_XTRACEFD", None)
                environment.pop("PS4", None)
        result["frozen_build_actual_exit"] = child_code
        with full_build_lock(build):
            after = inspect_build(build, args.release)
        put(execution / "full_build.after.json", after)
        dep_after = dependencies(execution, "dependencies_after", sysroot, environment)
        put(execution / "dependencies.after.json", dep_after)
        require(before == after and dep_before == dep_after and frozen_files() == originals, "正式来源、完整对象、包或QtTest依赖前后变化")
        require(child_code == 0, "冻结专项构建真实退出非0；保留日志和目录，不借旧产物")
        out = execution / "build_01"
        require(regular(out / "build_exit.txt").read_text().strip() == "0", "冻结脚本内部退出码不是0")
        binary, private = out / "sample_add_review", out / "lib/libQt5Test.so.5"
        elf(binary, {2, 3})
        require(digest(private) == dep_before["fixed_library"]["sha256"], "复制私有QtTest库与实际链接库不一致")
        require(digest(binary) != before["formal_binary"]["sha256"], "专项不能冒称正式主程序")
        commands = []
        for line in (execution / "build.xtrace.log").read_text(encoding="utf-8").splitlines():
            match = re.fullmatch(r"\++ (?:.+?:[0-9]+: )?(aarch64-linux-gnu-g\+\+ .+)", line)
            if match:
                commands.append(shlex.split(match[1]))
        require(len(commands) == 2 and "-c" in commands[0] and "-c" not in commands[1], "未取得唯一实际编译和链接参数；不称来源完整")
        put(execution / "actual_compile_link_commands.json", {"commands": commands, "basis": "独立BASH_XTRACEFD，不混入原stdout/stderr"})
        require(command(execution, "checker_elf_dynamic", ["aarch64-linux-gnu-readelf", "-h", "-d", binary], environment) == 0, "专项ELF依赖元数据读取失败")
        dynamic = (execution / "checker_elf_dynamic.stdout.log").read_text(encoding="utf-8")
        require("libQt5Test.so.5" in dynamic, "专项动态库未声明QtTest，未放行")
        result.update(compiled=True, build_evidence_complete=True, checker_binary=record(binary), private_qt_test=record(private),
                      formal_release=args.release, formal_binary_sha256=before["formal_binary"]["sha256"], actual_exit=0)
    except BaseException as error:
        result.update(actual_exit=1, error=str(error), frozen_build_actual_exit=child_code,
                      recovery="仅保留；缺件须由Root从匹配的07_backups/qt_environment正式归档恢复，再用全新namespace执行")
    put(execution / "wrapper_result.json", result)
    put_text(execution / "wrapper.exit", str(result["actual_exit"]) + "\n")
    with (execution / "WRAPPER_SHA256SUMS").open("x", encoding="ascii", newline="\n") as stream:
        for path in sorted(execution.rglob("*")):
            if path.is_file() and path.name != "WRAPPER_SHA256SUMS":
                stream.write(digest(path) + "  " + path.relative_to(execution).as_posix() + "\n")
    print("SAMPLE_CHECKER15_WRAPPER_EXIT=" + str(result["actual_exit"]))
    print("EVIDENCE=" + str(execution))
    return result["actual_exit"]


if __name__ == "__main__":
    raise SystemExit(main())
