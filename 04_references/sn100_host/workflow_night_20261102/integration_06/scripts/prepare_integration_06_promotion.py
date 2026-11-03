"""Root显式给最终merge_review SHA后才生成06晋级脚本；本工具不晋级或构建。"""
from pathlib import Path, PurePosixPath
import argparse
import ast
import hashlib
import json
import os
import pprint
import re
import stat

BASE_SHA256 = "e785702f907aecee9bc645614f122c05f1c6053d33ecb1b94f7e08ee8eaaf111"
TEMPLATE_SHA256 = "1c2f1a82e8b065d772d26eccdf372451d150c879ddb6a460fd0129d7e7d9181a"
PATCHES = (
    ("report_lis/channel_switch_candidate_v2/channel_switch.patch", "bdae1b3d7a0220ac77ff588e515988b2e9687f5a1689864653c95edf0445465d"),
    ("project_update_intents/project_update_intents.patch", "250b88f58b0e10b365aa3ae4a0c0005eef2e3619676e6fe659b607aa7822963c"),
    ("report_lis/qq_actual_count_review/qq_count_tail.patch", "9d53133db944e6ce430c50cd926933ef16ef263d1f3e4291efb0117984cc64a9"),
    ("project_copy_v2/copy_core_and_ui.patch", "a4ab1ade63b6bb558bb856874f36718ffb1ef65eaad7ad11af1b8de322656ef8"),
    ("project_copy_v2/wiring.patch", "9e57b4817616ae7d7220dcf47c6c4e419312787c195ab12465e540a8ce011e76"),
    ("project_copy_v2/external_writer_guards.patch", "6c49a885f73671b5f732c38a734121dab011b27ed229c8b658f598ea23e7001b"),
    ("integration_06_bridges/copy_cal_guard_after_intents.patch", "eb048b9d6bd69b445256cf7e3a012aecffe5251e35980ddd0ab20c0d5b8cc351"),
    ("project_update_intents_v2/delta_from_250b.patch", "68b5f47ece7f544a8200a792ec15ee8baaff2876a9de93911f9ce3139eb9bfd9"),
)
REQUIRED_NEW = {
    "instrument/original_channel_receiver.cpp", "instrument/original_channel_receiver.h",
    "instrument/original_channel_store.cpp", "instrument/original_channel_store.h",
    "projects/original_project_copy.cpp", "projects/original_project_copy.h",
    "projects/original_project_copy_dialog.cpp", "projects/original_project_copy_dialog.h",
    "projects/copyprojectdialog.ui", "projects/original_project_save_notice.h",
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def regular(path):
    if not stat.S_ISREG(path.lstat().st_mode):
        raise RuntimeError("不是普通输入文件：" + str(path))
    return path.read_bytes()


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError("已核05模板位置不唯一或已变化：" + before[:80])
    return text.replace(before, after, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--merge-review-sha256", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-f0-9]{64}", args.merge_review_sha256):
        raise RuntimeError("必须显式传最终已审merge_review的64位SHA-256。")
    task = Path(__file__).resolve().parent
    root = task.parents[2]
    output = task / "promote_integration_06_review.py"
    if output.exists() or output.is_symlink():
        raise RuntimeError("06固化晋级脚本已存在，拒绝覆盖或重复生成。")
    raw_review = regular(task / "integration_06_review/merge_review.json")
    if sha(raw_review) != args.merge_review_sha256:
        raise RuntimeError("当前merge_review不是Root显式给定的最终版本。")
    review = json.loads(raw_review)
    ordered = tuple((x["patch"], x["sha256"]) for x in review["applied_patches"])
    if (ordered != PATCHES or review["source_base"] != "integration_05/source_snapshot.json"
            or review["base_files"] != 639 or review["formal_source_modified"] is not False
            or review["compiled"] is not False or review["program_executed"] is not False):
        raise RuntimeError("必须是完整已审八片（含意图v2 delta）的06副本，不接受浮动七片。")
    base_raw = regular(task / "integration_05/source_snapshot.json")
    if sha(base_raw) != BASE_SHA256:
        raise RuntimeError("固定05源码清单已变化。")
    base = json.loads(base_raw)
    if len(base) != 639:
        raise RuntimeError("05底稿不是639项。")
    prefix = "06_programs/sn100_host/"
    added = set()
    for name, expected in PATCHES:
        raw = regular(task / name)
        if sha(raw) != expected:
            raise RuntimeError("生成前固定补丁已变化：" + name)
        lines = raw.decode("utf-8").splitlines()
        for i, line in enumerate(lines):
            if not line.startswith("--- "):
                continue
            if i + 1 >= len(lines) or not lines[i + 1].startswith("+++ "):
                raise RuntimeError("补丁文件头不完整。")
            before, after = line[4:].split("\t", 1)[0], lines[i + 1][4:].split("\t", 1)[0]
            for value in (before, after):
                if value == "/dev/null":
                    continue
                relative = value[2:]
                if (not value.startswith(("a/", "b/")) or not relative.startswith(prefix)
                        or ".." in PurePosixPath(relative).parts or "\\" in relative or ":" in relative):
                    raise RuntimeError("补丁目标越界或不是项目相对路径。")
            if before == "/dev/null":
                added.add(after[2 + len(prefix):])
            elif after == "/dev/null":
                added.discard(before[2 + len(prefix):])
    if added != REQUIRED_NEW:
        raise RuntimeError("新增目标不是已核4+5+1项，不能漏文件或扩大范围。")
    for name, expected in base.items():
        if sha(regular(root / prefix / name)) != expected:
            raise RuntimeError("生成前正式05输入已变化：" + name)
    for name in added:
        target = root / prefix / name
        if target.exists() or target.is_symlink():
            raise RuntimeError("应新增目标已有文件，保留原件并拒绝：" + name)
    template = regular(task / "promote_integration_06_template.py.in")
    if sha(template) != TEMPLATE_SHA256:
        raise RuntimeError("05沿用模板已变化，未生成。")
    source = template.decode("utf-8")
    source = replace_once(source, 'BASE_SHA256 = "209a9fd27636d611ca691e6fc0c18059351d15dbd232e7729bf90089d3e1c5cd"',
                          'BASE_SHA256 = "' + BASE_SHA256 + '"\nREVIEW_SHA256 = "' + args.merge_review_sha256 + '"')
    begin, end = source.index("PATCHES = ("), source.index("\ntask = ")
    source = source[:begin] + "PATCHES = " + pprint.pformat(PATCHES, width=110) + "\n" + source[end:]
    source = source.replace("integration_05_review", "integration_06_review")
    source = source.replace("integration_04/source_snapshot.json", "integration_05/source_snapshot.json")
    source = source.replace("sn100-integration-05-promotion-v1", "sn100-integration-06-promotion-v1")
    source = source.replace("FORMAL_PATCHES_APPLIED=4", "FORMAL_PATCHES_APPLIED=8")
    source = source.replace("第五轮四片", "第六轮八片").replace("第四轮全部底稿和四片", "第五轮639项底稿和八片")
    source = source.replace("第四轮固定底稿", "第五轮固定底稿").replace("本次已审四片", "本次已审八片")
    source = replace_once(source, '    review = json.loads(review_bytes)',
        '    if digest(review_bytes) != REVIEW_SHA256:\n        raise RuntimeError("06隔离副本清单已变化。")\n    review = json.loads(review_bytes)')
    ending = '"deleted_files": [name for name, value in final.items() if expected[name] is not None and value is None]})'
    source = replace_once(source, ending, ending[:-2] + ',\n'
        '                         "final_source_files": {name[len(PREFIX):]: value for name, value in final.items() if value is not None},\n'
        '                         "promotion_script_sha256": digest(read_regular(Path(__file__)))})')
    source = replace_once(source,
        '    raise SystemExit("仅为06晋级模板；请先用prepare_integration_06_promotion.py生成。")', '    main()')
    ast.parse(source)
    if sha(regular(task / "integration_06_review/merge_review.json")) != args.merge_review_sha256:
        raise RuntimeError("生成期间merge_review变化，未写输出。")
    with output.open("xb") as stream:
        stream.write(source.encode("utf-8"))
        stream.flush()
        os.fsync(stream.fileno())
    print("PROMOTION_06_SCRIPT_GENERATED=1\nFORMAL_SOURCE_MODIFIED=0\nBUILD_EXECUTED=0")
    print("PROMOTER_SHA256=" + sha(regular(output)))


if __name__ == "__main__":
    main()
