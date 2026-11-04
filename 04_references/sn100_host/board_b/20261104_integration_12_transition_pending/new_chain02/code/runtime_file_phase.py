"""只读核对第12批文件阶段原件和完整程序包，供外部生成器调用。"""
from pathlib import Path
from contextvars import ContextVar
import hashlib
import json
import re
import stat
import tarfile

SHA = re.compile(r"[a-f0-9]{64}\Z")
PREFIX = "sn100-integration-12-01-"
WORK_NAME = "temp/sn100_host/workflow_next_20261103/integration_12_01"
ARCHIVE_NAME = "07_backups/sn100_host/board_b/workflow_integration_20261103/integration_12_01"
EVIDENCE_NAME = "04_references/sn100_host/workflow_next_20261103/integration_12_01"
_READ_RECORDS = ContextVar("runtime_file_phase_read_records", default=None)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def pairs_no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "JSON含重复键：" + key)
        result[key] = value
    return result


def decode_json(raw):
    def invalid_constant(value):
        raise ValueError("JSON不接受NaN或Infinity：" + value)
    return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs_no_duplicates, parse_constant=invalid_constant)


def safe_name(value):
    require(isinstance(value, str) and value and not any(c in value for c in "\\:\0\r\n")
            and not value.startswith("/") and all(p not in ("", ".", "..") for p in value.split("/")),
            "不是工程内规范相对路径：" + repr(value))
    return value


def regular(path, root):
    path, root = Path(path), Path(root).resolve()
    path.resolve().relative_to(root)
    for parent in (path, *path.parents):
        require(not parent.is_symlink() and not (hasattr(parent, "is_junction") and parent.is_junction()),
                "路径经过链接：" + str(parent))
        if parent == root:
            break
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1, "不是单链接普通文件：" + str(path))
    tracked = _READ_RECORDS.get()
    if tracked is not None:
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
        item = {"path": path.relative_to(root).as_posix(), "sha256": digest.hexdigest(), "bytes": path.stat().st_size}
        require(item["path"] not in tracked or tracked[item["path"]] == item, "校验期间原件变化：" + item["path"])
        tracked[item["path"]] = item
    return path


def fingerprint(path, root):
    digest = hashlib.sha256()
    with regular(path, root).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def check_record(item, root):
    require(isinstance(item, dict) and set(item) == {"path", "sha256", "bytes"}, "原件必须有准确的path/SHA/bytes。")
    require(isinstance(item["sha256"], str) and SHA.fullmatch(item["sha256"])
            and item["sha256"] != "0" * 64 and type(item["bytes"]) is int and item["bytes"] > 0,
            "原件指纹或字节数为空/类型不符。")
    path = regular(Path(root) / safe_name(item["path"]), root)
    require(path.stat().st_size == item["bytes"] and fingerprint(path, root) == item["sha256"], "原件指纹或字节数不符：" + item["path"])
    return path


def read_record(item, root):
    return decode_json(check_record(item, root).read_bytes())


def require_record_equal(item, expected, root):
    check_record(item, root)
    require(item == expected, "文件阶段相互引用了不同的原件。")


def actual_zero(item, root):
    raw = check_record(item, root).read_bytes()
    require(raw in (b"0\n", b"0\r\n"), "实际阶段退出码不是0：" + item["path"])


def snapshot(value):
    require(isinstance(value, dict) and value, "缺少完整源码表。")
    for name, digest in value.items():
        safe_name(name)
        require(isinstance(digest, str) and SHA.fullmatch(digest), "源码指纹格式错误。")
    return value


def verify_package(path, build, sources, target, root):
    """流式回读全部成员，只暂存三份小元数据，不解压或执行程序。"""
    members, special = {}, {}
    with tarfile.open(regular(path, root), "r:gz") as archive:
        for member in archive:
            name = safe_name(member.name)
            if member.isdir():
                continue
            require(member.isfile() and name not in members, "程序包含特殊或重复条目：" + name)
            source = archive.extractfile(member)
            require(source is not None, "不能回读程序包条目。")
            digest, data = hashlib.sha256(), bytearray()
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
                if name in ("SHA256SUMS", "release_id", "release.json"):
                    data.extend(block)
                    require(len(data) <= 2 * 1024 * 1024, "包内元数据超过约束。")
            members[name] = digest.hexdigest()
            if name in ("SHA256SUMS", "release_id", "release.json"):
                special[name] = bytes(data)
    require(set(special) == {"SHA256SUMS", "release_id", "release.json"}, "程序包缺少完整发布元数据。")
    release = build["release_id"]
    require(hashlib.sha256(special["SHA256SUMS"]).hexdigest() == release
            and special["release_id"] == (release + "\n").encode("ascii"), "包内发布号与完整清单不符。")
    wanted = {}
    for line in special["SHA256SUMS"].decode("ascii").splitlines():
        digest, name = line.split("  ", 1)
        safe_name(name)
        require(SHA.fullmatch(digest) and name not in wanted, "包内清单指纹无效或重复。")
        wanted[name] = digest
    require({k: v for k, v in members.items() if k not in ("SHA256SUMS", "release_id")} == wanted,
            "程序包成员与完整清单不等。")
    metadata = decode_json(special["release.json"])
    require(metadata.get("sources") == sources and metadata.get("binary_sha256") == build["binaries"]["sn100_shell"]
            and metadata.get("renderer_sha256") == build["binaries"]["sn100_report_renderer"]
            and metadata.get("dependencies_sha256") == build["dependency_report_sha256"], "包内来源不是本次构建。")
    for name, digest in build["binaries"].items():
        require(members.get(name) == digest, "包内程序指纹不等于构建终态：" + name)
    require(members.get("rk_board.sh") == target["helper_sha256"], "包内helper指纹不等于冻结目标。")
    return {"members": members, "metadata": metadata, "member_count": len(members),
            "helper_sha256": members["rk_board.sh"], "manifest_sha256": release}


def _verify_file_phase(core, root):
    """先完成所有已有事实校验；本模块不创建输出、不调用任何外部命令。"""
    root = Path(root).resolve()
    phase = core["file_phase"]
    names = {"config", "source_manifest", "build_complete", "object_binding", "dependency_report", "stage_complete",
             "archive_complete", "collection_complete", "finish_complete", "package", "actual_exit_records"}
    require(isinstance(phase, dict) and set(phase) == names, "文件阶段输入字段不完整或含未知项。")
    paths = {name: check_record(phase[name], root) for name in names - {"actual_exit_records"}}
    require(paths["config"].name == "integration_12_01_tools_config.json", "本轮配置文件名错误。")
    work = root / WORK_NAME
    for name in ("build_complete", "object_binding", "stage_complete", "archive_complete", "collection_complete", "finish_complete"):
        require(paths[name] == work / (name + ".json"), "文件阶段记录不在本轮WORK：" + name)
    require(paths["dependency_report"] == work / "dependencies/dependency_report.json", "依赖报告不是本轮WORK原件。")
    exits = phase["actual_exit_records"]
    require(isinstance(exits, dict) and set(exits) == {"build", "stage", "archive", "collect", "finish"}, "缺实际阶段退出原件。")
    for name, item in exits.items():
        exit_path = check_record(item, root)
        if name == "build":
            require(exit_path == work / "build_exit.txt", "编译退出原件不在本轮WORK。")
        else:
            require(exit_path.parent == work / "logs" and re.fullmatch("root_" + name + r"_[0-9]+\.exit", exit_path.name),
                    "外层退出原件不属于本轮对应阶段：" + name)
        actual_zero(item, root)
    values = {name: read_record(phase[name], root) for name in names - {"actual_exit_records", "package"}}
    config, build = values["config"], values["build_complete"]
    require(config.get("format") == PREFIX + "tools-config-v2" and config.get("frozen") is True, "文件阶段配置尚未冻结。")
    permissions = config.get("operation_permissions")
    require(isinstance(permissions, dict) and set(permissions) == {"allow_full_build", "allow_stage"}
            and all(flag is True for flag in permissions.values()), "文件阶段构建/存包未获准。")
    require_record_equal(config["final_source_manifest"], phase["source_manifest"], root)
    sources = snapshot(values["source_manifest"])
    require(build.get("format") == PREFIX + "build-complete-v1" and build.get("arm64_build_and_package_passed") is True
            and build.get("program_executed") is False and build.get("board_runtime_verified") is False
            and type(build.get("source_count")) is int and build["source_count"] == len(sources)
            and build.get("tools_config_sha256") == phase["config"]["sha256"],
            "完整构建终态与已冻结配置/源码不一致。")
    source_raw = regular(work / "source_snapshot.json", root)
    require(fingerprint(source_raw, root) == build["source_snapshot_sha256"] and snapshot(decode_json(source_raw.read_bytes())) == sources,
            "构建原始源码表与冻结字典不等；原文指纹不能混用。")
    require(build.get("promotion_progress_sha256") == config["promotion_progress"]["sha256"]
            and build.get("final_merge_review_sha256") == config["final_review"]["sha256"], "构建来源账/最终审查不符。")
    check_record(config["promotion_progress"], root)
    check_record(config["final_review"], root)
    require_record_equal(build["object_binding"], phase["object_binding"], root)
    require(build.get("dependency_report_sha256") == phase["dependency_report"]["sha256"], "依赖原件SHA不符。")
    dependency = values["dependency_report"]
    require(dependency.get("errors") == [] and dependency.get("offline_link_and_sql_closure_passed") is True
            and dependency.get("board_runtime_verified") is False, "静态依赖没有完整通过。")
    target = core["target"]
    release = build.get("release_id")
    require(isinstance(release, str) and SHA.fullmatch(release) and release != "0" * 64
            and target["release_id"] == release and target["nested_namespace"] == "integration_12_01", "新目标发布号无效或混批。")
    require(set(build["binaries"]) == {"sn100_shell", "sn100_report_renderer"}
            and build["binaries"]["sn100_shell"] == target["application_sha256"]
            and build["binaries"]["sn100_report_renderer"] == target["renderer_sha256"], "双程序目标指纹不一致。")
    require(paths["package"] == work / "packages" / ("sn100_shell_" + release + ".tar.gz")
            and build["package"] == paths["package"].name and build["package_sha256"] == phase["package"]["sha256"], "实际程序包路径/SHA错误。")
    require(regular(work / "build_exit.txt", root).read_bytes() == b"0\n", "完整构建真实退出不是0。")
    log = regular(work / "logs/build.log", root).read_text(encoding="utf-8").splitlines()
    require(log.count("ARM64_INTEGRATION_12_01_BUILD_AND_PACKAGE_PASSED=1") == 1, "构建未实际完整封口。")
    stage, archive, collection, finish = (values[name] for name in ("stage_complete", "archive_complete", "collection_complete", "finish_complete"))
    for name, value in (("stage", stage), ("archive", archive), ("collection", collection), ("finish", finish)):
        require(value.get("format") == PREFIX + name + "-complete-v1" and value.get("complete") is True
                and value.get("release_id") == release, "文件阶段尚未完整封口或混批：" + name)
    require(stage.get("board") == "board_b" and stage.get("serial") == core["board_serial"]
            and stage.get("current_before") == config["expected_current"] == stage.get("current_after")
            and stage.get("remote_release") == core["base"] + "/releases/integration_12_01/" + release
            and type(stage.get("actual_upload_exit")) is int and stage["actual_upload_exit"] == 0
            and type(stage.get("actual_stage_exit")) is int and stage["actual_stage_exit"] == 0
            and stage.get("activation_performed") is False and stage.get("program_started") is False
            and stage.get("package_sha256") == phase["package"]["sha256"], "存包板卡、退出或保留运行范围不符。")
    for key in ("config", "build_complete", "object_binding", "dependency_report", "package"):
        if key in stage["inputs"]:
            require_record_equal(stage["inputs"][key], phase[key], root)
    require_record_equal(stage["inputs"]["config"], phase["config"], root)
    require_record_equal(stage["inputs"]["build_complete"], phase["build_complete"], root)
    for item in stage["results"].values():
        check_record(item, root)
    for name in ("upload.exit", "board_b_stage.exit"):
        require(regular(work / name, root).read_bytes() == b"0\n", "上传或板端存包真实退出不是0。")
    guard = stage["configuration_guard"]
    expected_manifest = config["protected_configuration_manifest"]
    require_record_equal(guard["expected_manifest"], expected_manifest, root)
    expected_raw = check_record(expected_manifest, root).read_bytes()
    require(guard.get("format") == PREFIX + "configuration-guard-v1" and guard.get("unchanged") is True
            and check_record(guard["before"], root).read_bytes() == expected_raw == check_record(guard["after"], root).read_bytes(), "七配置存包保护不符。")
    live = stage["live_guard"]
    require(live.get("format") == PREFIX + "live-guard-v2" and live.get("unchanged") is True
            and check_record(live["before"], root).read_bytes() == check_record(live["after"], root).read_bytes(), "存包运行保护原文不等。")
    require_record_equal(collection["config_record"], phase["config"], root)
    require_record_equal(collection["stage_complete_record"], phase["stage_complete"], root)
    require_record_equal(collection["archive_complete_record"], phase["archive_complete"], root)
    require_record_equal(finish["collection_complete_record"], phase["collection_complete"], root)
    require_record_equal(finish["archive_complete_record"], phase["archive_complete"], root)
    require(archive.get("build_complete_sha256") == phase["build_complete"]["sha256"], "归档构建来源不符。")
    for value in (archive, collection, finish):
        require_record_equal(value["object_binding_record"], phase["object_binding"], root)
    require(collection.get("configuration_guard") == guard == finish.get("configuration_guard"), "收证/收尾丢失同一配置保护。")
    require(finish.get("current_changed") is False and finish.get("program_started") is False
            and finish.get("rk_function_verified") is False and finish.get("instrument_verified") is False, "文件阶段结果被扩大为运行验收。")
    archive_dir, evidence_dir = root / ARCHIVE_NAME, root / EVIDENCE_NAME
    require(archive.get("archive_path") == ARCHIVE_NAME and collection.get("evidence_path") == EVIDENCE_NAME, "归档或证据目录混批。")
    require(fingerprint(archive_dir / "SHA256SUMS", root) == archive["archive_checksums_sha256"]
            and fingerprint(evidence_dir / "SHA256SUMS", root) == collection["evidence_checksums_sha256"], "归档/证据清单SHA不符。")
    require_record_equal(finish["archive_checksums_record"], {"path": ARCHIVE_NAME + "/SHA256SUMS", "sha256": archive["archive_checksums_sha256"], "bytes": (archive_dir / "SHA256SUMS").stat().st_size}, root)
    require_record_equal(finish["evidence_checksums_record"], {"path": EVIDENCE_NAME + "/SHA256SUMS", "sha256": collection["evidence_checksums_sha256"], "bytes": (evidence_dir / "SHA256SUMS").stat().st_size}, root)
    provenance_path = regular(archive_dir / "provenance.json", root)
    require(fingerprint(provenance_path, root) == archive["provenance_sha256"], "归档来源说明SHA不符。")
    provenance = decode_json(provenance_path.read_bytes())
    require(provenance.get("stage_complete") == stage and provenance.get("release_id") == release
            and provenance.get("package_sha256") == phase["package"]["sha256"], "归档来源说明不是同一次存包。")
    require(fingerprint(archive_dir / paths["package"].name, root) == phase["package"]["sha256"], "归档程序包不等于WORK原件。")
    package_info = verify_package(paths["package"], build, sources, target, root)
    return {"config": config, "build": build, "stage": stage, "archive": archive, "collection": collection,
            "finish": finish, "package_info": package_info, "sources": sources, "paths": paths, "work": work,
            "package_path": paths["package"], "configuration_manifest_raw": expected_raw, "provenance": provenance}


def verify_file_phase(core, root):
    """返回实际读取的全部原件记录，调用方在写输出前再次复核。"""
    tracked = {}
    token = _READ_RECORDS.set(tracked)
    try:
        result = _verify_file_phase(core, root)
        for item in tuple(tracked.values()):
            regular(Path(root).resolve() / item["path"], Path(root).resolve())
        result["read_records"] = [tracked[name] for name in sorted(tracked)]
        return result
    finally:
        _READ_RECORDS.reset(token)
