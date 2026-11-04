"""Root冻结两片真实采用的已审输入；不重放补丁、不编译或联板。"""
from pathlib import Path
import argparse
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'temp/sn100_host/workflow_next_20261103/source_ledger_inputs_14_01'


def record(path):
    raw = path.read_bytes()
    return {'path': path.relative_to(ROOT).as_posix(), 'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--expected-review-sha256', required=True)
    args = parser.parse_args()
    assert re.fullmatch(r'[a-f0-9]{64}', args.expected_review_sha256)
    draft = OUT / 'root_spec_14.draft_01.json'
    assert record(draft)['sha256'] == 'af823fc38bd61d3a5c9984067f5440f76479635b47f8e4b9a1ae3e39a47570d0'
    review = HERE / 'source_ledger_14_independent_review_01.md'
    assert record(review)['sha256'] == args.expected_review_sha256
    approval = OUT / 'root_source_approval_14_01.md'
    assert record(approval)['sha256'] == '231b1ef13370f14b3ac6cdf10d4e946333a8f19c8a181ed0b9c28a227cde7d42'
    spec = json.loads(draft.read_bytes())
    assert spec['owner'] == 'root' and spec['frozen'] is False
    assert spec['all_intended_source_slices_present'] is False
    assert spec['root_approval'] is None and spec['independent_input_review'] is None
    assert spec['historical_failed_checks'] is None
    assert spec['required_patch_names'] == ['lis_a', 'sample_add_order'] and len(spec['patches']) == 2
    assert spec['final_source_manifest']['sha256'] == '169f571193b2893e8cbb3df8359c723e414cff89232ccc6e5d3a9274c44022bd'
    assert record(ROOT / spec['final_source_manifest']['path']) == spec['final_source_manifest']
    assert spec['git_apply_config']['post_adoption_readonly_environment'] is None
    assert spec['root_attributes']['sha256'] == record(ROOT / '.gitattributes')['sha256'] == 'c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a'
    target_directory = 'temp/sn100_host/workflow_next_20261103/root_source_ledger_final_14_01'
    assert spec['output_directory'] == target_directory and not (ROOT / target_directory).exists()
    spec.update(frozen=True, all_intended_source_slices_present=True,
                root_approval=record(approval), independent_input_review=record(review),
                historical_failed_checks=[], output_directory=target_directory,
                pending_root_confirmation=[], selected_unfrozen_draft=record(draft),
                root_freeze_notice='Root按两片真实0/0汇总697/169f；不重apply，不预填编译或板端结果。13冻结与旧运行79/4原件保留。')
    target = OUT / 'root_spec_14.frozen_01.json'
    with target.open('xb') as stream:
        stream.write((json.dumps(spec, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    print(json.dumps({'frozen_spec': record(target), 'ledger_executed': False, 'board_operated': False}))


if __name__ == '__main__':
    main()
