"""锁定draft02，补总控批准和独审，另存本批冻结输入。"""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
FOLDER = HERE / 'source_ledger_inputs_12_01'


def record(path):
    raw = path.read_bytes()
    return {'path': path.relative_to(ROOT).as_posix(), 'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--review-sha', required=True)
    args = parser.parse_args()
    draft = FOLDER / 'root_spec_12.draft_02.json'
    assert record(draft)['sha256'] == '0e775cbb6d30a38713fb9f86b9cebc5d8f0f9023a6924fa668aebf05f0a1a989'
    review = HERE / 'source_ledger_12_independent_review_01.md'
    assert record(review)['sha256'] == args.review_sha
    approval = FOLDER / 'root_source_approval_688_12_01.md'
    spec = json.loads(draft.read_text(encoding='utf-8'))
    assert spec['frozen'] is False and spec['root_approval'] is None
    assert len(spec['patches']) == 1 and spec['patches'][0]['name'] == 'ascii_uh'
    assert not (ROOT / spec['output_directory']).exists()
    assert spec['baseline_archive']['sha256'] == 'd709196b4fa1f33af57c7f038abdf2134341bd5619617f3c9d94b8144055dd96'
    assert spec['baseline_snapshot']['sha256'] == '387348d0a0dfb77cb5c1640432044d3e283dfe8f3a427e946568ba8a6352a676'
    assert spec['final_source_manifest']['sha256'] == '0e5c70ef9d5ac5fa3db34888103b4158783141aac5f90845e225c720d532ac8d'
    assert record(ROOT / spec['ledger_script']['path']) == spec['ledger_script']
    assert not spec['historical_failed_checks']
    spec['root_approval'] = record(approval)
    extras = [record(path) for path in (draft, review, approval, Path(__file__).resolve(),
              HERE / 'uh12_adoption_preflight_review_01.md', FOLDER / 'coverage_handoff_12_02.md')]
    for suffix in ('.sh', '.log', '.exit', '.started'):
        extras.append(record(ROOT / 'temp/sn100_host/root_manager_20261103' /
                             ('prebuild_live_integration12_20261104_01' + suffix)))
    extras.append(record(ROOT / 'temp/sn100_host/root_manager_20261103/board08.py'))
    assert (ROOT / 'temp/sn100_host/root_manager_20261103/prebuild_live_integration12_20261104_01.exit').read_text().strip() == '0'
    supporting = spec['patches'][0]['supporting_records']
    existing = {item['path']: item for item in supporting}
    for item in extras:
        if item['path'] in existing:
            assert existing[item['path']] == item
        else:
            supporting.append(item)
            existing[item['path']] = item
    for item in supporting:
        assert record(ROOT / item['path']) == item, item['path']
    spec['approved_unfrozen_input'] = record(draft)
    spec['independent_input_review'] = record(review)
    spec['pending_root_confirmation'] = []
    spec['all_intended_source_slices_present'] = True
    spec['draft_notes'] = {'selected_draft': record(draft), 'draft03_not_selected': True,
                           'root_confirmed_at': datetime.now(timezone.utc).isoformat(),
                           'approval_uses_current_exact_bytes': True,
                           'post_adoption_git_capture_scope_approved': True,
                           'historical_draft_nulls_not_rewritten': True}
    spec['scope_notice'] = 'Root批准实际1片Uh六修改来源聚合；当前688/0e5c，未编译新批或运行产品，C7保持。急诊恢复资料用户列待办，LIS保存/ACK及算法不纳本账。'
    spec['frozen'] = True
    target = FOLDER / 'root_spec_12.frozen_01.json'
    with target.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(spec, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write('\n')
    print(json.dumps(record(target)))


if __name__ == '__main__':
    main()
