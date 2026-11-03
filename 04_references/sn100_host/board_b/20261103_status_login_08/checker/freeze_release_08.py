#!/usr/bin/env python3
"""最终08正式构建成功后，从匹配的官方源码归档准备专项输入。

用法：freeze_release_08.py ABSOLUTE_INTEGRATION_08 RELEASE_ID OFFICIAL_SOURCE_TAR
本工具不从仍可编辑的正式源码目录复制文件，不编译、不运行、不连接板卡。
准备中途失败会保留明确的暂存目录，不能覆盖后重用旧证据。
"""

import argparse
import hashlib
import os
import tarfile
from datetime import datetime, timezone
from pathlib import Path

from bind_build_08 import (
    CONTRACT_NAMES, HERE, ROOT, SCHEMA, TEMPLATE_NAMES, build_lock, integration_path,
    new_json, read_json, regular, relative_name, require, sha, validate_release_id,
    verify_frozen, verify_stage,
)
from shutdown_contract_08 import make_contract, write_contract
from task_shutdown_contract_adapter_candidate import augment_task_shutdown_contract


ARCHIVE_PREFIX = "06_programs/sn100_host/"


def official_archive(path, release_id, identity, snapshot):
    path = Path(path)
    require(path.is_absolute() and regular(path).resolve() == path, "官方源码归档须为未重定向的绝对路径")
    require(path.is_relative_to(ROOT / "07_backups/sn100_host/board_b")
            and "integration_08" in path.parts, "源码必须来自板B最终08正式归档目录")
    require(path.name == "sn100_sources_" + release_id + ".tar.gz", "源码归档名称不属于指定发布")
    archive_sha = sha(path)
    archive_snapshot = read_json(path.parent / "source_snapshot.json")
    require(archive_snapshot == snapshot, "官方归档的源码清单与最终08包不一致")
    provenance = read_json(path.parent / "provenance.json")
    require(provenance.get("release_id") == release_id
            and provenance.get("source_archive_sha256") == archive_sha
            and provenance.get("application_sha256") == identity["application_sha256"]
            and provenance.get("source_files") == len(snapshot)
            and provenance.get("intended_board") == "board_b"
            and provenance.get("build_and_dependencies") == "passed",
            "官方归档来源说明与本次08构建不一致")
    manifest = regular(path.parent / "SHA256SUMS").read_text().splitlines()
    archive_entry = archive_sha + "  " + path.name
    require(manifest.count(archive_entry) == 1, "官方SHA256SUMS没有唯一匹配的源码归档")
    # 不使用extract/extractall。先查每个成员的名字、类型、范围和内容，再逐文件写入。
    with tarfile.open(path, "r:gz") as archive:
        members = archive.getmembers()
        seen = set()
        total = 0
        for member in members:
            require(member.isfile() and not member.issym() and not member.islnk(),
                    "源码归档只能含普通文件：" + member.name)
            require(member.name.startswith(ARCHIVE_PREFIX), "源码归档成员不属于SN100：" + member.name)
            name = relative_name(member.name[len(ARCHIVE_PREFIX):])
            require(name in snapshot and name not in seen, "源码归档成员多余、重复或越界：" + name)
            require(0 <= member.size <= 128 * 1024 * 1024, "源码单文件大小异常，先审查归档：" + name)
            total += member.size
            require(total <= 2 * 1024 * 1024 * 1024, "源码归档展开大小异常，先审查归档")
            stream = archive.extractfile(member)
            require(stream is not None, "无法读取源码归档成员：" + name)
            digest = hashlib.sha256()
            with stream:
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(block)
            require(digest.hexdigest() == snapshot[name], "归档源码不等于最终snapshot：" + name)
            seen.add(name)
        require(seen == set(snapshot), "官方源码归档缺文件")
    return path, archive_sha


def copy_archive_files(archive_path, destination):
    require(not destination.exists(), "冻结暂存目录已存在")
    destination.mkdir(parents=True)
    with tarfile.open(archive_path, "r:gz") as archive:
        for member in archive.getmembers():
            require(member.isfile() and member.name.startswith(ARCHIVE_PREFIX), "归档在冻结期间改变")
            name = relative_name(member.name[len(ARCHIVE_PREFIX):])
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            require(target.parent.resolve().is_relative_to(destination), "冻结文件目标越界")
            source = archive.extractfile(member)
            require(source is not None, "无法读取冻结文件：" + name)
            with source, target.open("xb") as output:
                for block in iter(lambda: source.read(1024 * 1024), b""):
                    output.write(block)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("integration_directory", type=Path)
    parser.add_argument("release_id")
    parser.add_argument("source_archive", type=Path)
    args = parser.parse_args()
    release_id = validate_release_id(args.release_id)
    integration = integration_path(args.integration_directory)
    output_names = ("frozen_08", "prepared_inputs.json", "source_snapshot_08.json") + CONTRACT_NAMES
    with build_lock(integration):
        for name in output_names:
            require(not (HERE / name).exists() and not (HERE / name).is_symlink(), "最终输入已存在，不能覆盖：" + name)
        for name in ("link_provenance.json", "link_objects.txt", "status_login_review_08", "review_release_id"):
            require(not (HERE / name).exists(), "已有检查器或链接证据，不能重新冻结覆盖")
        identity, snapshot, _, _ = verify_stage(integration, release_id)
        templates = {name: sha(HERE / name) for name in TEMPLATE_NAMES}
        archive_path, archive_sha = official_archive(args.source_archive, release_id, identity, snapshot)
        staging = HERE / ("freeze_staging_08_" + str(os.getpid()))
        require(not staging.exists(), "冻结暂存目录已存在，先核对先前失败证据")
        staging.mkdir()
        staged_frozen = staging / "frozen_08/sn100_host"
        copy_archive_files(archive_path, staged_frozen)
        verify_frozen(staged_frozen, snapshot)
        new_json(staging / "source_snapshot_08.json", snapshot)
        approved = read_json(HERE / "approved_final_merge_review.json")
        require(sha(HERE / "approved_final_merge_review.json") == "62683e9babba255ae1f2bf58a4a69b5961a7a7bdcd5c59d1a36ef847a6527eac"
                and approved["final_source_files"] == snapshot, "批准审查与官方源码不一致")
        contract = make_contract(staged_frozen, release_id, HERE / "run_case_on_board.sh")
        contract = augment_task_shutdown_contract(contract, staged_frozen, snapshot,
                    approved["final_source_files"], sha(HERE / "approved_final_merge_review.json"))
        write_contract(contract, staging)
        contract_hashes = {name: sha(staging / name) for name in CONTRACT_NAMES}
        after_identity, after_snapshot, _, _ = verify_stage(integration, release_id)
        require(after_identity == identity and after_snapshot == snapshot, "冻结期间正式06输入改变")
        require(sha(archive_path) == archive_sha, "冻结期间官方源码归档改变")
        require({name: sha(HERE / name) for name in TEMPLATE_NAMES} == templates, "冻结期间准备工具或检查器改变")
        prepared = {
            "schema": SCHEMA, "review_generation": "08", "status": "bound_to_final_08_archive",
            "created_at_utc": datetime.now(timezone.utc).isoformat(),
            "release_id": release_id, "integration_directory": str(integration),
            "formal_identity": identity, "source_program": identity["source_program"],
            "source_count": len(snapshot), "object_count": None,
            "source_archive": str(archive_path), "source_archive_sha256": archive_sha,
            "archive_provenance_sha256": sha(archive_path.parent / "provenance.json"),
            "archive_manifest_sha256": sha(archive_path.parent / "SHA256SUMS"),
            "frozen_directory": str(HERE / "frozen_08/sn100_host"),
            "frozen_snapshot_sha256": sha(staging / "source_snapshot_08.json"),
            "prepared_checker_sha256": templates["status_login_review_08.cpp"],
            "ad_wire_observer_sha256": templates["ad_wire_observer.h"],
            "template_sha256": templates, "contract_sha256": contract_hashes,
            "observation_scope": read_json(HERE / "observation_scope_08.json"),
            "object_binding": "bind_build_08.py读取实际build_inputs后填写link_provenance；未硬编码数量。",
            "product_built": False, "product_executed": False, "board_accessed": False,
        }
        new_json(staging / "prepared_inputs.json", prepared)
        # prepared_inputs最后发布；没有它就不能继续绑定或构建。
        (staging / "frozen_08").rename(HERE / "frozen_08")
        for name in ("source_snapshot_08.json",) + CONTRACT_NAMES + ("prepared_inputs.json",):
            source = staging / name
            require(not (HERE / name).exists() and not (HERE / name).is_symlink(), "发布冻结证据时发现冲突：" + name)
            os.link(source, HERE / name)
            source.unlink()
        staging.rmdir()
        print("FINAL_08_INPUTS_FROZEN=" + release_id + "; SOURCE_FILES=" + str(len(snapshot)))
        print("PRODUCT_BUILT=0; PRODUCT_EXECUTED=0; BOARD_ACCESSED=0")


if __name__ == "__main__":
    main()
