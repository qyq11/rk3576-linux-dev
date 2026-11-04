"""保存实际关闭、清理与只读核查原件；不运行产品、不连接板卡。"""
from pathlib import Path
import hashlib, shutil, json

ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
OUT=ROOT/'04_references/sn100_host/board_a/20261104_close_and_cleanup'
OUT.mkdir(exist_ok=False)
names=[]
for prefix in ('close_requested_20261104_01','freeze_question_readonly_01','input_window_readonly_02','popup_state_readonly_03','popup_state_readonly_04'):
    names.extend(p.name for p in HERE.glob(prefix+'.*'))
names.extend(['cleanup_object_files_01.ps1','object_cleanup_before_01.json','object_cleanup_result_01.json','hold_cleanup_locks_01.sh','commit_archive_audit_01.py','commit_archive_audit_01.json','commit_archive_audit_02.py','commit_archive_audit_02.json'])
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
for name in sorted(set(names)):
    src=HERE/name
    dst=OUT/name
    shutil.copyfile(src,dst)
    assert sha(src)==sha(dst),name
readme='''# 板A关闭、编译中间文件清理与提交前核对

本目录保存2026-11-04用户要求“把程序关掉，清理一下中间文件并commit push”后的真实收尾原件。只关闭上位机，未恢复其他功能任务。

- `close_requested_20261104_01.*`：再次核对板A已知SSH公钥、序列3bf8222fe1417f6a、开机/程序身份和实际配置后，只正常关闭一次。SSH及guarded-close均0，原运行目录20261104_153722_5015实际APP_EXIT=0/CLEAN_SHUTDOWN=1，线程结束、各存储关闭，GUI5836已不存在；原主控4426及5000监听保持。日志中的旧SHUTDOWN_REJECTED保留，最终退出封口另列，不能混淆。脚本仅为执行原件，不能从此目录重跑。
- `freeze_question_readonly_01.*`、`input_window_readonly_02.*`：关闭前针对“所有按钮点不动、Esc未恢复”的只读核查，不能证明界面交互通过。`popup_state_readonly_03.*`和`04.*`是未取得结果的失败原件，真实退出分别141、1，不作成功解释。具体原因仍未定位。
- `cleanup_object_files_01.ps1`、`object_cleanup_before_01.json`、`object_cleanup_result_01.json`：仅删除第11/12/14/15批完整构建与两个样本专项的1182个ARM64对象文件，释放2157479552字节（约2.16GB）。四个完整构建锁事先持有，清理后释放。逐份删除前路径、大小、SHA保留；无递归目录删除，源码、候选、日志、工具链/sysroot和正式恢复输入保留。旧构建成功标志不再证明对象可复用，下次需完整重建。
- `commit_archive_audit_01.*`保留首次核查工具失败：它错误地要求inputs中的来源清单副本同时带齐所有原目录成员。`02.*`修正为按原仓库位置核成员并比对清单副本自身，实际退出0，22份清单/1644条成员通过，697正式源无漂移；未检出私钥/访问令牌。没有运行电脑产品或新增板端功能检查。

此前9项软件通过、容器选择超时1项及人工界面无响应均保留原结论，取消/放弃、实体操作、真实仪器、资源门槛和长稳未验。本轮关闭后，不能再假定先前两个空架仍是运行态现场；没有另发清架命令。

192.168.100.123随后由用户改接小车板B，Root已释放该地址协调，不再凭旧IP操作板A。后续上板必须重新按已知公钥及实际硬件序列确认。全部SN100功能任务继续暂停；样本窗口另按用户新授权准备保全未合入候选，本目录不把候选视为生产采用。

同一过程记录见 `02_records/20261022_sn100_host_migration.md#close-cleanup-commit-20261104`。SHA256SUMS只封本目录原件，提交/推送真实结果由Git及最终交接给出，不预填成功。本目录原始脚本保留当时相对路径，供审查，不作当前可执行入口。
'''
(OUT/'README.md').write_text(readme,encoding='utf-8',newline='\n')
shutil.copyfile(Path(__file__),OUT/Path(__file__).name)
entries=sorted(p for p in OUT.rglob('*') if p.is_file() and p.name!='SHA256SUMS')
(OUT/'SHA256SUMS').write_text(''.join(f'{sha(p)}  {p.relative_to(OUT).as_posix()}\n' for p in entries),encoding='utf-8',newline='\n')
print(json.dumps({'files':len(entries),'manifest_sha256':sha(OUT/'SHA256SUMS'),'output':OUT.relative_to(ROOT).as_posix()},ensure_ascii=False))
