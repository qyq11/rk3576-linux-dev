"""按JSON九工具清单复制冻结，仅准完整编译与另存，C7留驻。"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
TASK = ROOT / 'temp/sn100_host/workflow_next_20261103'
SOURCE = TASK / 'build_archive_candidate_12_01'
DEST = TASK / 'integration_12_root_frozen_01'
LEDGER = HERE / 'root_source_ledger_final_12_01'
SPEC = HERE / 'source_ledger_inputs_12_01/root_spec_12.frozen_01.json'
REPORT = HERE / 'build_archive_12_independent_review_01.md'
TOOL_LIST = HERE / 'build_archive_12_independent_review_01.tools.json'
NINE = {'integration_12_01_tool_support.py', 'build_integration_12_01.sh',
        'verify_integration_12_01_inputs.py', 'integration_12_01_object_binding.py',
        'stage_board_b_release_12_01.py', 'stage_board_b_release_12_01.sh',
        'archive_integration_12_01.py', 'verify_and_save_integration_12_01.py',
        'finish_integration_12_01_archive.py'}


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def record(path):
    raw = path.read_bytes()
    return {'path': path.relative_to(ROOT).as_posix(), 'sha256': sha(raw), 'bytes': len(raw)}


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def future_record(path, raw):
    return {'path': path.relative_to(ROOT).as_posix(), 'sha256': sha(raw), 'bytes': len(raw)}


def main():
    assert not DEST.exists() and not (TASK / 'integration_12_01').exists()
    assert record(REPORT)['sha256'] == '32b6d8cf5f0fa1df295cb442fe40111bac7d392251d223553438877973f4b585'
    assert record(TOOL_LIST)['sha256'] == '1515aa15d996633bdbf21dadbe6f206411647864adc4043c1aec9ea094e11e06'
    approved = read(TOOL_LIST)
    assert len(approved) == 9 and {Path(item['path']).name for item in approved} == NINE
    blobs = {}
    for item in approved:
        path = ROOT / item['path']
        assert path.parent == SOURCE and record(path) == item
        blobs[path.name] = path.read_bytes()
    for name in ('README.md', 'basis_inventory.json', 'operation_contract.md'):
        if (SOURCE / name).is_file():
            blobs[name] = (SOURCE / name).read_bytes()
    assert 'README.md' in blobs and 'basis_inventory.json' in blobs
    assert (HERE / 'source_ledger_run_12_01.exit').read_text().strip() == '0'
    spec = read(SPEC)
    review = read(LEDGER / 'final_review.json')
    progress = read(LEDGER / 'promotion_progress.json')
    final = read(ROOT / spec['final_source_manifest']['path'])
    assert len(final) == 688
    assert spec['frozen'] is True and spec['all_intended_source_slices_present'] is True
    assert review['aggregation_only'] is True and review['executed_git_apply'] is False
    assert review['source_count'] == 688 and len(review['patches']) == 1
    assert review['final_source_files'] == final and len(review['changed_files']) == 6
    assert progress['complete'] is True and progress['phase'] == 'complete'
    assert progress['verified_source_paths'] == 688 and progress['final_source_files'] == final
    assert progress['source_snapshot_sha256'] == '387348d0a0dfb77cb5c1640432044d3e283dfe8f3a427e946568ba8a6352a676'
    template = SOURCE / 'integration_12_01_tools_config.json'
    assert record(template)['sha256'] == '8152b1154806ef6c9019020bee534cf7dbf8c256539620f4b7f00672a2fb4b46'
    config = read(template)
    assert config['frozen'] is False and config['operation_permissions'] == {'allow_full_build': False, 'allow_stage': False}
    for key in ('baseline_archive', 'baseline_snapshot'):
        assert config[key] == spec[key]
    assert config['expected_current'] == 'releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e'
    assert record(ROOT / '.gitattributes')['sha256'] == 'c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a'
    config['frozen'] = True
    config['purpose'] = '第12批精确Uh六文件完整ARM64编译及板B独立存包归档；C7保持运行，不切current、不启停产品或原主控。急诊恢复资料列待办。'
    config['approved_final_review_sha256'] = record(LEDGER / 'final_review.json')['sha256']
    config['metadata_schema'] = {'review_format': review['format'], 'promotion_format': progress['format']}
    config['final_review'] = record(LEDGER / 'final_review.json')
    config['promotion_progress'] = record(LEDGER / 'promotion_progress.json')
    config['promotion_script'] = spec['ledger_script']
    config['final_source_manifest'] = spec['final_source_manifest']
    config['operation_permissions'] = {'allow_full_build': True, 'allow_stage': True}
    config['tools'] = [future_record(DEST / name, blobs[name]) for name in sorted(NINE)]
    records = read(LEDGER / 'required_cfg_evidence.json')
    extras = [record(path) for path in (REPORT, TOOL_LIST, template, SPEC,
              LEDGER / 'final_review.json', LEDGER / 'promotion_progress.json',
              HERE / 'source_ledger_12_independent_review_01.md',
              HERE / 'integration12_leave_running_contract_review_01.md', Path(__file__).resolve(),
              ROOT / '04_references/sn100_host/workflow_next_20261103/.gitattributes',
              ROOT / '07_backups/sn100_host/board_b/workflow_integration_20261103/.gitattributes')]
    extras += [record(HERE / ('source_ledger_run_12_01' + suffix)) for suffix in ('.stdout.log', '.stderr.log', '.exit')]
    unique = {}
    for item in [*records, *config['evidence'], *extras]:
        actual = record(ROOT / item['path'])
        assert actual['sha256'] == item['sha256'] and ('bytes' not in item or actual['bytes'] == item['bytes'])
        if item['path'] in unique:
            assert unique[item['path']] == actual
        unique[item['path']] = actual
    for name in blobs.keys() - NINE:
        item = future_record(DEST / name, blobs[name])
        assert item['path'] not in unique
        unique[item['path']] = item
    config['evidence'] = sorted(unique.values(), key=lambda item: item['path'])
    # 所有外部输入先实核，再创建冻结目录；此后只写已核原字节。
    DEST.mkdir()
    for name, raw in blobs.items():
        with (DEST / name).open('xb') as stream:
            stream.write(raw)
        assert record(DEST / name) == future_record(DEST / name, raw)
    target = DEST / 'integration_12_01_tools_config.json'
    with target.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(config, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    note = {'format': 'sn100-root-file-phase-tool-freeze-v1', 'execution_namespace': 'integration_12_01',
            'source_count': 688, 'config': record(target), 'source_code_copies_equal': True,
            'compiled': False, 'staged': False, 'activated': False, 'application_started': False,
            'leave_c7_running_required': True, 'evidence_records': len(config['evidence'])}
    with (DEST / 'root_freeze.json').open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(note, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    print(json.dumps(note, ensure_ascii=False))


if __name__ == '__main__':
    main()
