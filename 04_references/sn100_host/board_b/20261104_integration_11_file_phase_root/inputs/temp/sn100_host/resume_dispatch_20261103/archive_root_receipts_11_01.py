"""另存总控收尾回执；不改已冻结202原件，不连接板卡。"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
WORK = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_11_01'
OUT = ROOT / '04_references/sn100_host/board_b/20261104_integration_11_file_phase_root'


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def save(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(raw)


def main():
    assert not OUT.exists()
    assert json.loads((WORK / 'finish_complete.json').read_text())['complete'] is True
    receipt = {'format': 'sn100-root-observed-tool-facts-v1', 'created_at': datetime.now(timezone.utc).isoformat(),
               'chat_id': '01a1a05a-73c4-7380-b761-c35a7fdbf76e',
               'source': 'Root实际观察到的工具结束结果，非重新创建的进程标准输出',
               'original_freezer01_call_exit': None, 'original_freezer01_output_incomplete': True,
               'captured_freezer01_retry_actual_exit': 1, 'freezer02_actual_exit': 0,
               'tool_results': [
                   {'phase': 'build', 'session_id': 35598, 'completion_chunk_id': '25c7f7', 'actual_exit': 0},
                   {'phase': 'stage', 'session_id': 56151, 'completion_chunk_id': 'e649a5', 'actual_exit': 0},
                   {'phase': 'archive', 'session_id': 93173, 'completion_chunk_id': '28a4a2', 'actual_exit': 0},
                   {'phase': 'collect', 'session_id': 98401, 'completion_chunk_id': '56bef4', 'actual_exit': 0},
                   {'phase': 'finish', 'session_id': 73255, 'completion_chunk_id': '3f2780', 'actual_exit': 0}],
               'product_executed_by_this_archiver': False, 'board_operated_by_this_archiver': False}
    fact = HERE / 'root_file_phase_tool_facts_11_01.json'
    save(fact, (json.dumps(receipt, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    paths = [Path(__file__).resolve(), fact,
             ROOT / 'temp/sn100_host/workflow_next_20261103/integration_11_root_frozen_01/root_freeze.json',
             WORK / 'finish_complete.json']
    paths += [HERE / name for name in (
        'freeze_file_phase_tools_11_01.py', 'freeze_file_phase_tools_11_02.py',
        'build_archive_11_frozen_binding_review_01.md', 'build_artifact_independent_review_11_01.md',
        'stage_leave_running_independent_review_11_01.md', 'final_checks_batch11_01.py', 'final_checks_batch11_01.json')]
    paths += [HERE / (stem + suffix) for stem in (
        'freeze_file_phase_tools_11_01.retry_01', 'freeze_file_phase_tools_11_02', 'final_git_diff_check_11_01')
        for suffix in ('.stdout.log', '.stderr.log', '.exit')]
    paths += [WORK / ('logs/root_' + phase + '_01' + suffix)
              for phase in ('stage', 'archive', 'collect', 'finish')
              for suffix in ('.stdout.log', '.stderr.log', '.exit')]
    records = []
    for path in paths:
        assert path.is_file() and not path.is_symlink()
        raw = path.read_bytes()
        name = path.relative_to(ROOT).as_posix()
        save(OUT / 'inputs' / name, raw)
        records.append({'source_path': name, 'sha256': sha(raw), 'bytes': len(raw)})
    save(OUT / '.gitattributes', b'* -text\n')
    save(OUT / 'source_records.json', (json.dumps(records, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    readme = '''# 第11批总控执行回执和后补独立审查

本目录补充保存总控四个文件阶段的真实输出、退出、最终完成记录、后补独审，以及冻结入口01的捕获重试失败和02最小修正结果。主证据仍是04_references/sn100_host/workflow_next_20261103/integration_11_01的202份清单，本目录没有修改那批原件。

Root实际观察的工具事实与进程原始stdout/stderr分开：第一次冻结调用的完整输出未保留，退出记null；捕获重试明确退出1，02退出0。build/stage/archive/collect/finish工具结束均实际0，不将本JSON称为原始进程输出。

新356fe5f8包只另存板B独立目录，current仍C7；锁内前后同669382/2102645、原主控5008/118180及七配置指纹。未切版、未启停，不包含新版本功能、真实仪器、实体或资源验证通过结论。Root恢复文件在07_backups/sn100_host/board_b/workflow_integration_20261103/integration_11_01。

inputs按仓库相对路径保存精确字节，source_records.json记录来源。SHA256SUMS逐份核对全部文件；.gitattributes只在本目录禁用文本换行转换，根属性不变。没有INI正文、数据库或认证材料。本归档器未连接板卡或运行产品。
'''
    save(OUT / 'README.md', readme.encode('utf-8'))
    sums = ''.join(sha(path.read_bytes()) + '  ' + path.relative_to(OUT).as_posix() + '\n'
                   for path in sorted(OUT.rglob('*')) if path.is_file())
    save(OUT / 'SHA256SUMS', sums.encode('utf-8'))
    for row in sums.splitlines():
        digest, name = row.split('  ', 1)
        assert sha((OUT / name).read_bytes()) == digest
    print(json.dumps({'path': OUT.relative_to(ROOT).as_posix(), 'source_records': len(records),
                      'checksum_files': len(sums.splitlines()), 'checksums_sha256': sha(sums.encode()),
                      'product_executed': False, 'board_operated': False}))


if __name__ == '__main__':
    main()
