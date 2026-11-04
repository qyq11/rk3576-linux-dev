"""复制已审九文件并绑定真实688来源；只允许完整构建和另存，不切版。"""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
TASK = ROOT / 'temp/sn100_host/workflow_next_20261103'
SOURCE = TASK / 'build_archive_candidate_11_01'
DEST = TASK / 'integration_11_root_frozen_01'
LEDGER = HERE / 'root_source_ledger_final_11_01'
SPEC = HERE / 'source_ledger_inputs_11_01/root_spec_11.frozen_01.json'
REPORT = HERE / 'build_archive_11_independent_review_01.md'
NINE = ['integration_11_01_tool_support.py', 'build_integration_11_01.sh',
        'verify_integration_11_01_inputs.py', 'integration_11_01_object_binding.py',
        'stage_board_b_release_11_01.py', 'stage_board_b_release_11_01.sh',
        'archive_integration_11_01.py', 'verify_and_save_integration_11_01.py',
        'finish_integration_11_01_archive.py']
EXTRA = ['operation_contract.md', 'README.md', 'basis_inventory.json']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def record(path):
    return {'path': path.relative_to(ROOT).as_posix(), 'sha256': sha(path), 'bytes': path.stat().st_size}


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    assert not DEST.exists() and not (TASK / 'integration_11_01').exists()
    assert sha(REPORT) == 'c227f5f7553f9ef1856985cb841931ccc715e53b2a2d149ca843cb285a737151'
    approved = dict(re.findall(r'\|\s*`?([A-Za-z0-9_.-]+)`?\s*\|\s*`?([0-9a-f]{64})`?\s*\|', REPORT.read_text()))
    for name in NINE:
        assert name in approved and sha(SOURCE / name) == approved[name], name
    assert sha(LEDGER / 'final_review.json') == '1c71afdec3568e547ace6193c533e68d499f132e890aa0817d54500729251abb'
    assert sha(LEDGER / 'promotion_progress.json') == '8470f2abccda6a475ee7f6e7e31caaa58a5c299f99990815534d42cea3252ed8'
    assert (HERE / 'source_ledger_run_11_01.exit').read_text().strip() == '0'
    spec = read(SPEC)
    assert spec['frozen'] is True and spec['all_intended_source_slices_present'] is True
    template = SOURCE / 'integration_11_01_tools_config.json'
    assert sha(template) == 'e9298635adc2d85f00165b14d3c6027c759082e82870d4831ceeff70bce2064a'
    config = read(template)
    assert config['frozen'] is False and not any(config['operation_permissions'].values())
    assert sha(ROOT / '.gitattributes') == 'c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a'
    DEST.mkdir()
    for name in NINE + EXTRA:
        with (DEST / name).open('xb') as stream:
            stream.write((SOURCE / name).read_bytes())
        assert sha(DEST / name) == sha(SOURCE / name)
    config['frozen'] = True
    config['purpose'] = '第11批688源完整ARM64编译及板B独立存包归档；当前c7持续运行，不切current、不启停产品或原主控。'
    config['approved_final_review_sha256'] = sha(LEDGER / 'final_review.json')
    config['metadata_schema'] = {'review_format': read(LEDGER / 'final_review.json')['format'],
                                 'promotion_format': read(LEDGER / 'promotion_progress.json')['format']}
    config['final_review'] = record(LEDGER / 'final_review.json')
    config['promotion_progress'] = record(LEDGER / 'promotion_progress.json')
    config['promotion_script'] = spec['ledger_script']
    for key in ('baseline_archive', 'baseline_snapshot', 'final_source_manifest'):
        config[key] = spec[key]
    config['operation_permissions'] = {'allow_full_build': True, 'allow_stage': True}
    config['tools'] = [record(DEST / name) for name in NINE]
    records = read(LEDGER / 'required_cfg_evidence.json')
    extras = [record(REPORT), record(template), record(SPEC), record(LEDGER / 'final_review.json'),
              record(LEDGER / 'promotion_progress.json'), config['observation_contract']['root_decision'],
              config['protected_configuration_manifest']]
    for path in [HERE / 'source_ledger_11_independent_review_01.md', Path(__file__).resolve(),
                 *[HERE / ('source_ledger_run_11_01' + suffix) for suffix in ('.stdout.log', '.stderr.log', '.exit')],
                 *[ROOT / 'temp/sn100_host/root_manager_20261103' / ('prebuild_live_integration11_20261104_01' + suffix)
                   for suffix in ('.sh', '.log', '.exit', '.started')],
                 ROOT / 'temp/sn100_host/root_manager_20261103/board08.py',
                 ROOT / '04_references/sn100_host/workflow_next_20261103/.gitattributes',
                 ROOT / '07_backups/sn100_host/board_b/workflow_integration_20261103/.gitattributes']:
        extras.append(record(path))
    extras.extend(record(DEST / name) for name in EXTRA)
    unique = {}
    for item in [*records, *config['evidence'], *extras]:
        actual = record(ROOT / item['path'])
        assert actual['sha256'] == item['sha256']
        if 'bytes' in item:
            assert actual['bytes'] == item['bytes']
        if item['path'] in unique:
            assert unique[item['path']] == actual
        unique[item['path']] = actual
    config['evidence'] = sorted(unique.values(), key=lambda item: item['path'])
    target = DEST / 'integration_11_01_tools_config.json'
    with target.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(config, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    note = {'format': 'sn100-root-file-phase-tool-freeze-v1', 'execution_namespace': 'integration_11_01',
            'source_count': 688, 'config': record(target), 'source_code_copies_equal': True,
            'compiled': False, 'staged': False, 'activated': False, 'application_started': False,
            'leave_c7_running_required': True, 'evidence_records': len(config['evidence'])}
    with (DEST / 'root_freeze.json').open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(note, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    print(json.dumps(note, ensure_ascii=False))


if __name__ == '__main__':
    main()
