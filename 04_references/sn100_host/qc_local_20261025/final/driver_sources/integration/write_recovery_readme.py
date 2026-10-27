"""Describe the actual verified ARM pair, without claiming data/system recovery."""
import argparse
import hashlib
import json
from pathlib import Path

if not __debug__:
    raise RuntimeError('Recovery-input verification requires Python assertions enabled')

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root-go', action='store_true', required=True)
args = parser.parse_args()
assert args.root_go
work = Path(__file__).resolve().parent.parent
root = work.parents[2]
dest = root / '07_backups/sn100_host/offline_four_windows_20261025'
v = json.loads((dest / 'verification.json').read_text())
assert v['source_inputs'] == 426 and not v['runtime_data_or_authentication_archived']
freeze_sha = hashlib.sha256((dest / 'source_snapshot.json').read_bytes()).hexdigest()
assert freeze_sha == '5123bf1442472450bbde23a59fc47287dc85ead34e006c523a96a016ac76cf29'
arm = json.loads((dest / 'arm64_verification.json').read_text())
assert arm['source_freeze_sha256'] == freeze_sha and arm['source_before_after_and_package_equal_native_freeze']
assert arm['package']['release_id'] == v['release_id']
for key in ('application', 'source'):
    record = v[key]
    assert hashlib.sha256((dest / record['path']).read_bytes()).hexdigest() == record['sha256']
assert v['source']['members'] == 426
path = dest / 'README.md'
assert not path.exists()
body = f'''# SN100四窗口本地切片：匹配应用与源码恢复输入

本目录归档本轮ARM64应用和426项正式编译/发布输入，release_id=`{v['release_id']}`。来源为当前正式工程，426输入冻结SHA=`5123bf1442472450bbde23a59fc47287dc85ead34e006c523a96a016ac76cf29`；原始参考工程只读。实际全量交叉编译、离线依赖和包内成员校验已通过，未部署或运行RK，未连接仪器或真实LIS。

- 应用包：`{v['application']['path']}`，SHA-256 `{v['application']['sha256']}`，{v['application']['bytes']}字节。
- 源码包：`{v['source']['path']}`，SHA-256 `{v['source']['sha256']}`，严格426个文件成员，每个与`source_snapshot.json`一致。只含匹配构建/发布输入，不是全部测试、记录、依赖或仓库备份。
- 应用内部`SHA256SUMS`共{v['internal_members_verified']}项，含{v['aarch64_elf_members']}个ARM64 ELF；`arm64_verification.json`保留真实编译/链接/依赖/打包收据索引及工具、sysroot、源码前后校验。
- 此前371、361、353、348及本轮失败检查点各自保留，不能以旧包替代当前质控/校准版本。`pre_*`源码备份用于复现修前问题，不是运行数据库回滚包。

恢复输入先在新隔离目录按本目录`SHA256SUMS`核对文件，再核压缩包路径/成员和内部清单。正式`shell/rk.sh`是日常构建部署入口，其中deploy会prepare重新构建当前源码，不是直接选择本目录旧包的恢复命令；从此归档release恢复的实际写入步骤及数据schema兼容性须另行核对，本轮未执行恢复。应用范围只限该release及私有库/插件/字体/模板，不覆盖系统Qt、Mesa、libc或驱动。依赖安装来源与匹配环境材料沿用`07_backups/qt_environment/`中的Qt ARM64、SN100运行库/报表/样本等已有清单；本轮只读复用既有sysroot，没有安装、恢复或修改系统环境。静态依赖检查不证明板端加载通过。

源码应解到新的项目副本做对照，不覆盖当前带未提交修改的工作目录。WSL、Windows32、ARM64的构建目录、对象和库必须分开。对应源码的实际功能/平台/资源证据见[最终总索引](../../../04_references/sn100_host/qc_local_20261025/final/verification.json)，操作见[工程说明](../../../06_programs/sn100_host/README.md#offline-four-window-usage)。

本包不含运行数据库、INI、患者/生产数据、账户/密码/私钥，也不恢复系统、板配置、驱动或完整开发SDK。数据备份和应用回退/schema兼容性必须另行核对；不能用本包宣称多库数据恢复已经通过。

质控/校准数据库的UUID、规范物理路径与catalog保护登记必须配套。恢复或迁移前停止全部相关写入程序；原路径恢复匹配数据，换目录/换平台/克隆须显式核对迁移，包括隐藏历史及项目/试剂关联。遇到不匹配应保持写入禁用并核查，不直接清UUID、路径键或引用保护，也不因本轮匿名新库测试通过就推定旧生产库兼容。

本地规则/原始点/批组参数保存不代表临床算法、校准/ISI/QC效力、真实结果、实际LIS或实体打印通过；原Windows完整行为/主题、RK实屏/输入、仪器、认可预算/同板实时控制与长稳继续待验。两压缩包使用仓库Git LFS规则，主过程统一见[移植记录](../../../02_records/20261022_sn100_host_migration.md#offline-four-windows-final-20261026)。
'''
with path.open('x', encoding='utf-8', newline='\n') as stream:
    stream.write(body)
print(json.dumps({'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'source_inputs': 426}, ensure_ascii=False))
