"""Root核真实14来源账及九代码后创建配置；只准全编和独立存包，不含启停。"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
TASK = ROOT / 'temp/sn100_host/workflow_next_20261103'
DEST = TASK / 'integration_14_root_frozen_01'
LEDGER = TASK / 'root_source_ledger_final_14_01'
INPUTS = TASK / 'source_ledger_inputs_14_01'


def read(path):
    return json.loads(path.read_bytes())


def record(path):
    path.resolve().relative_to(ROOT.resolve())
    assert path.is_file() and not path.is_symlink() and path.stat().st_nlink == 1
    raw = path.read_bytes()
    return {'path': path.relative_to(ROOT).as_posix(), 'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw)}


def pinned(path, digest):
    value = record(path)
    assert value['sha256'] == digest, str(path)
    return value


def main():
    target = DEST / 'integration_14_01_tools_config.json'
    note_path = DEST / 'root_freeze.json'
    assert not target.exists() and not note_path.exists() and not (TASK / 'integration_14_01').exists()
    snapshot_path = DEST / 'root_code_snapshot_14_01.json'
    pinned(snapshot_path, 'd1c169d322ce4188093d405e2b756958cce1451373c8e7179be815f8c46649da')
    code = read(snapshot_path)
    assert code['code_only_frozen'] is True and code['configuration_written'] is False
    assert len(code['code_records']) == 9
    for item in code['code_records']:
        path = ROOT / item['path']
        assert path.parent == DEST and record(path) == item
    spec_path = INPUTS / 'root_spec_14.frozen_01.json'
    pinned(spec_path, 'f84e04e189c1f8a20fa84e6c6cca5a5c900488e6efd0691f145279b014ed6643')
    spec = read(spec_path)
    assert spec['frozen'] is True and spec['all_intended_source_slices_present'] is True
    assert spec['required_patch_names'] == ['lis_a', 'sample_add_order']
    final_path = ROOT / spec['final_source_manifest']['path']
    assert record(final_path) == spec['final_source_manifest']
    assert record(final_path)['sha256'] == '169f571193b2893e8cbb3df8359c723e414cff89232ccc6e5d3a9274c44022bd'
    final = read(final_path)
    assert len(final) == 697
    review_path, progress_path = LEDGER / 'final_review.json', LEDGER / 'promotion_progress.json'
    pinned(review_path, 'dfbe4b040e46cc51f9dd809c768f5e58b370cf527eeba4a1758692c7210d9f86')
    pinned(progress_path, '7c26f4698cf617b16b56f05dce7c904629074ebf33829d768fb39aa29329484b')
    assert (HERE / 'source_ledger_run_14_01.exit').read_text().strip() == '0'
    review, progress = read(review_path), read(progress_path)
    assert review['aggregation_only'] is True and review['executed_git_apply'] is False
    assert review['source_count'] == 697 and len(review['patches']) == 2
    assert review['final_source_files'] == final
    assert set(review['changed_files']) == {'shell/build.sh', 'shell/package_arm64.py', 'shell/shell.pri',
                                           'samples/sample_page.cpp', 'samples/sample_page.h'}
    assert len(review['added_files']) == 9 and review['deleted_files'] == []
    assert progress['complete'] is True and progress['phase'] == 'complete'
    assert progress['verified_source_paths'] == 697 and progress['final_source_files'] == final
    assert progress['source_snapshot_sha256'] == spec['baseline_snapshot']['sha256']
    template = INPUTS / 'tools_config_14.draft_01.json'
    pinned(template, 'd31dfdfcab631c25f85763b3363c7bc27f6fa42c6a849d4737a3a30788bc1bc3')
    config = read(template)
    assert config['frozen'] is False
    assert config['operation_permissions'] == {'allow_full_build': False, 'allow_stage': False}
    for key in ('baseline_archive', 'baseline_snapshot', 'final_source_manifest'):
        assert config[key] == spec[key]
    assert config['expected_current'] == 'releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e'
    assert config['expected_running_application_sha256'] == '50d3f3b510772e717e456cc4e3e910060a82a6635b9d914f033e79f9ed6f0258'
    assert config['expected_controller_sha256'] == 'ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff'
    pinned(ROOT / '.gitattributes', 'c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a')
    config.update(frozen=True, purpose='第14批LIS A9与样本加号两源完整ARM64编译、独立存包归档；C7及原主控留驻，相机不恢复，不切版、不登录或运行专项。',
                  approved_final_review_sha256=record(review_path)['sha256'],
                  metadata_schema={'review_format': review['format'], 'promotion_format': progress['format']},
                  final_review=record(review_path), promotion_progress=record(progress_path),
                  promotion_script=spec['ledger_script'], final_source_manifest=spec['final_source_manifest'],
                  operation_permissions={'allow_full_build': True, 'allow_stage': True}, tools=code['code_records'])
    extras = [HERE / name for name in ('build_archive_14_tool_delta_review_01.md',
        'build_archive_14_tool_delta_review_01.tools.json', 'source_ledger_14_independent_review_01.md',
        'freeze_file_phase_tools_14_code_01.py', 'freeze_source_ledger_14_01.py', 'run_source_ledger_14_01.py')]
    extras += [template, spec_path, review_path, progress_path, LEDGER / 'required_cfg_evidence.json',
               snapshot_path, DEST / 'author_candidate_manifest.captured_01.json', Path(__file__).resolve(),
               INPUTS / 'root_source_approval_14_01.md',
               ROOT / '04_references/sn100_host/workflow_next_20261103/.gitattributes',
               ROOT / '07_backups/sn100_host/board_b/workflow_integration_20261103/.gitattributes']
    extras += [HERE / ('source_ledger_run_14_01' + suffix) for suffix in
               ('.command.json', '.stdout.log', '.stderr.log', '.exit')]
    camera = ROOT / '04_references/sn100_host/board_b/20261104_camera_stopped_baseline'
    assert len([path for path in camera.iterdir() if path.is_file()]) == 9
    extras += sorted(path for path in camera.iterdir() if path.is_file())
    pending = ROOT / '04_references/sn100_host/board_b/20261104_integration_12_transition_pending'
    extras += [pending / 'SHA256SUMS', pending / 'README.md']
    records = [*read(LEDGER / 'required_cfg_evidence.json'), *config['evidence'], *map(record, extras)]
    unique = {}
    for item in records:
        actual = record(ROOT / item['path'])
        assert actual['sha256'] == item['sha256'] and ('bytes' not in item or actual['bytes'] == item['bytes'])
        if item['path'] in unique:
            assert unique[item['path']] == actual
        unique[item['path']] = actual
    config['evidence'] = sorted(unique.values(), key=lambda item: item['path'])
    with target.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(config, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    note = {'format': 'sn100-root-file-phase-tool-freeze-v1', 'execution_namespace': 'integration_14_01',
            'source_count': len(final), 'config': record(target), 'source_code_copies_equal': True,
            'compiled': False, 'staged': False, 'activated': False, 'application_started': False,
            'leave_c7_running_required': True, 'evidence_records': len(config['evidence'])}
    with note_path.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(note, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    print(json.dumps(note, ensure_ascii=False))


if __name__ == '__main__':
    main()
