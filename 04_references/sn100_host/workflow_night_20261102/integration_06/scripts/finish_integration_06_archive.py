"""根据实际06证据写用途说明，并逐份复算清单及检查压缩包LFS属性。"""
from pathlib import Path
import hashlib
import json
import runpy
import subprocess


def main():
    task = Path(__file__).resolve().parent
    root = task.parents[2]
    backup = root / "07_backups/sn100_host/board_b/workflow_integration_20261102/integration_06"
    evidence = root / "04_references/sn100_host/workflow_night_20261102/integration_06"
    p = json.loads((backup / "provenance.json").read_text(encoding="utf-8"))
    v = json.loads((evidence / "verification.json").read_text(encoding="utf-8"))
    if v["arm64_build_exit"] != 0 or v["board_stage_exit"] != 0 or p["release_id"] != v["release_id"]:
        raise RuntimeError("本批构建/暂存证据不完整。")
    network = p.get("stage_network_recovery")
    if network is not None:
        if v.get("stage_network_recovery") != network or v.get("initial_board_stage_exit") != 255:
            raise RuntimeError("归档没有保留首次无线255及显式有线恢复链。")
        collector = task / "verify_and_save_integration_06.py"
        if collector.read_bytes() != (evidence / "scripts/verify_and_save_integration_06.py").read_bytes():
            raise RuntimeError("收尾工具与已归档实际collector不一致。")
        helper = runpy.run_path(str(collector), run_name="sn100_integration06_network_evidence_check")
        _, checked, copies = helper["resolve_board_stage"](p, network["complete_record"]["sha256"])
        if checked != network:
            raise RuntimeError("显式网络恢复实际输入/结果不再等于已归档终态。")
        for source, relative in copies.items():
            if source.read_bytes() != (evidence / relative).read_bytes():
                raise RuntimeError("网络失败/检查/恢复的归档原文不符：" + relative)
    elif (evidence / "board_b_stage.exit").read_text().strip() != "0":
        raise RuntimeError("首次保存未成功且没有显式网络恢复链。")
    if any((directory / "README.md").exists() for directory in (backup, evidence)):
        raise RuntimeError("本批用途说明已存在，先核查，不覆盖。")
    release = p["release_id"]
    scope = "这批接收主控通道开关（Uh）、按原声明数量读取Qq通道值、接上单项目参数同步意图的回执保护，以及本地项目完整复制和中断后的读取隔离。八片串行合入；普通保存/批组的新生产入口和N液面回报另批处理。"
    measured = (f"实际源码{v['source_count']}项、实际构建输入{v['unique_build_inputs']}项；变更{p['changed_files']}项，"
                f"其中新增{p['added_files']}、删除{p['deleted_files']}。主程序{v['compiled_objects']}个ARM64对象，"
                f"报表辅助程序另{v['helper_objects']}个对象；完整链接、静态依赖核对和打包退出0。"
                f"源码包逐份回读{v['source_archive_readback_files']}项，程序包逐份回读{v['package_archive_readback_files']}项一致。")
    boundary = (f"板卡为板B（board_b），硬件序列号`{v['serial']}`。独立版本目录的清单、两程序指纹和实际动态库核对退出0；"
                f"保存时current仍为`{p['current_release_at_staging']}`。没有切换current、运行本版或重启主控。"
                "RK界面、业务、资源、真实仪器及长期运行仍待原配置联机验证，不能把编译和文件核对当作功能通过。")
    recovery = ("恢复范围仅本批程序、匹配源码和静态依赖说明；不含原INI、患者、账户、工作数据库、工具链或整机镜像。"
                "恢复前先核板卡身份和现有环境；程序解到独立版本目录后核清单与动态库；源码解到单独工作目录后逐份核`source_snapshot.json`，不要覆盖后来的修改。"
                "本地复制不表示共享目录对端已同步；中断后有来源的克隆和清单保留，不能据本归档删除数据库或猜造校准曲线。")
    if "attribute_alignment_recovery" in p:
        scope += ("首次晋级在第4片已写入后，因temp未镜像正式表单的换行属性而核对失败；正式表单保持原候选LF字节。"
                  "根从第05批源码包按相同路径属性重演全部八片，确认正式恰等第4步后，只续第5—8片并全表逐字节核对。"
                  "原失败记录、换行诊断、0—8检查点、独立恢复工具和退出1但无对象/程序的首次预构建目录均保留；不称首次成功。")
    stage_description = "`board_b_stage.log`和`.exit`记录板B文件核对。"
    if network is not None:
        scope += ("首次无线上传退出0，保存SSH会话因连接重置退出255；同板有线只读检查确认原上传包完整、目标目录尚未形成、current和原主控不变后，"
                  "根用同一上传包和原脚本显式续保存，预检及保存实际退出0。原255和失败日志保留，不能把断线推为主控退出。")
        stage_description = ("`board_b_stage.log`和`.exit`保留首次无线保存255；`stage_network_recovery/stage.log`和`stage.exit`记录有线显式续保存0。"
                             "同目录保留在途/完成metadata、原无线检查失败、有线检查、原脚本、恢复工具及其逐份输入指纹。")
    if "collector_directory_recovery" in p:
        scope += ("首次归档器漏建子目录，实际退出1；已复制的7文件部分目录逐份核对后改名保留，修正复制路径后重新归档退出0。"
                  "首次失败证据来自根转存的当次真实工具返回，不是归档器自行写出的原日志，也不是重跑结果。")
    (backup / "README.md").write_text(f"# 板B第06批程序和匹配源码\n\n版本`{release}`。{scope}\n\n{measured}\n\n{boundary}\n\n程序包`sn100_shell_{release}.tar.gz`，源码包`sn100_sources_{release}.tar.gz`。指纹和来源见`provenance.json`、`SHA256SUMS`；两压缩包由Git LFS管理。\n\n{recovery}\n", encoding="utf-8", newline="\n")
    (evidence / "README.md").write_text(f"# 第06批晋级、构建和板B保存证据\n\n版本`{release}`。{scope}\n\n{measured}\n\n{boundary}\n\n`promotion_progress.json`记录普通补丁预演、逐片正式写入及新增/删除目标；`source_review/`保存原行为、保护差异和独立复核；`patches/`保存固定八片；`scripts/`保存实际工具。`build.log`、`build_exit.txt`、`build_complete.json`记录完整构建终态。{stage_description}构建工具原文的`offline_closure`仅指电脑静态分析ARM64依赖，没有运行电脑产品、匿名资料或模拟服务。\n\n{recovery}\n\n`SHA256SUMS`覆盖本目录除清单自身外的文件；逐份复算一致。不自动提交Git。\n", encoding="utf-8", newline="\n")
    for directory in (backup, evidence):
        rows = []
        for path in sorted(directory.rglob("*")):
            if path.is_symlink():
                raise RuntimeError("归档出现链接。")
            if path.is_file() and path.name != "SHA256SUMS":
                rows.append(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.relative_to(directory).as_posix())
        (directory / "SHA256SUMS").write_text("\n".join(rows) + "\n", encoding="ascii", newline="\n")
        for row in rows:
            expected, name = row.split("  ", 1)
            if hashlib.sha256((directory / name).read_bytes()).hexdigest() != expected:
                raise RuntimeError("逐份复算失败：" + name)
        print(str(directory.relative_to(root)) + "|CHECKED=" + str(len(rows)) + "|FILES=" + str(len(rows) + 1))
    packages = [path.relative_to(root).as_posix() for path in sorted(backup.glob("*.gz"))]
    result = subprocess.run(["git", "check-attr", "filter", "diff", "merge", "--", *packages], cwd=root,
                            check=True, stdout=subprocess.PIPE, text=True)
    print(result.stdout, end="")
    for package in packages:
        for name in ("filter", "diff", "merge"):
            if package + ": " + name + ": lfs" not in result.stdout.splitlines():
                raise RuntimeError("压缩包未受LFS管理：" + package)


if __name__ == "__main__":
    main()
