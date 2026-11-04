"""仅准备下一批稀疏组合和三侧冲突原件；不写正式源、构建或操作板卡。"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'temp/sn100_host/workflow_next_20261103/root_combined_candidate_11_01'
PROGRAM = ROOT / '06_programs/sn100_host'
MG = ROOT / 'temp/sn100_host/reagent_single_probe_20261103_04'
LIS = ROOT / 'temp/sn100_host/lis_profile_source_candidate_20261103_01'
MANIFEST = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01/ascii_b1/source_after.json'


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def read_at(base, name):
    path = base / name
    assert path.resolve().is_relative_to(base.resolve()) and not path.is_symlink()
    return path.read_bytes()


def save(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(raw)


def table(path):
    return json.loads(path.read_text())


def main():
    assert digest(MANIFEST.read_bytes()) == 'f741feea28fcb6a2bcff8d6c2bda49faa67d3b72e3508062e5f1e368b2422b8b'
    original = table(MANIFEST)
    assert len(original) == 687
    for name, expected in original.items():
        assert digest(read_at(PROGRAM, name)) == expected, name
    delivery_path = MG / 'delivery_manifest_04.json'
    assert digest(delivery_path.read_bytes()) == '1d187b679234c38d6ecd5215e42fc29836ac0240c7555aee917a2ebe8b1eddd5'
    delivery = table(delivery_path)
    mg_sources = delivery['source_sha256']
    assert len(mg_sources) == 13 and digest((MG / 'freeze_04.json').read_bytes()) == 'f1f21c931da5d4b1b2cbfbf0c02dbc8bc6c576647ef9c16bee1f2148c5efead7'
    mg_before = table(MG / 'base_selected_sources.json')
    for name, expected in mg_before.items():
        assert digest(read_at(MG / 'baseline/06_programs/sn100_host', name)) == expected
    drift = sorted(name for name in mg_before if original[name] != mg_before[name])
    assert drift == ['shell/device_control_shell.cpp', 'shell/frmmain.h']
    lis_sources = {
        'lis_transport/lis_connection_values.h': '41bb19561f6b3996107470107a59b04b01df1310fa1afb5abf16caf9a79b2051',
        'lis_transport/lis_connection_controller.cpp': '7ca09f58d332ac20bf0f7d234c7fc4081a5acddadf646484bba11d81f69c7b61',
    }
    assert digest((LIS / 'handoff.md').read_bytes()) == '4b1b6350202a13f0b71b23da5a606fa2002bd5935537029331de0a21be0b7823'
    OUT.mkdir()
    result = {'format': 'sn100-root-sparse-combination-preparation-v1',
              'time_utc': datetime.now(timezone.utc).isoformat(), 'base_source_count': 687,
              'base_manifest_sha256': digest(MANIFEST.read_bytes()), 'mg_base_overlap': drift,
              'formal_written': False, 'compiled': False, 'board_operated': False, 'merge_results': []}
    before = OUT / 'before/06_programs/sn100_host'
    candidate = OUT / 'candidate/06_programs/sn100_host'
    for name in sorted(set(mg_sources) | set(lis_sources) | {'shell/build.sh', 'reagents/reagents.pri'}):
        if name in original:
            save(before / name, read_at(PROGRAM, name))
    for name, expected in mg_sources.items():
        incoming = MG / 'candidate/06_programs/sn100_host' / name
        assert digest(incoming.read_bytes()) == expected
        if name in drift:
            args = ['git', '-c', 'core.autocrlf=false', 'merge-file', '--diff3', '-p',
                    '-L', 'current_f741_B1', '-L', 'baseline_b688', '-L', 'Mg_revision04',
                    str(before / name), str(MG / 'baseline/06_programs/sn100_host' / name), str(incoming)]
            merged = subprocess.run(args, cwd=ROOT, capture_output=True)
            assert 0 <= merged.returncode <= 127
            save(candidate / name, merged.stdout)
            stem = name.replace('/', '__')
            save(OUT / 'merge' / (stem + '.stderr.log'), merged.stderr)
            save(OUT / 'merge' / (stem + '.actual_exit'), (str(merged.returncode) + '\n').encode('ascii'))
            save(OUT / 'merge' / (stem + '.command.json'), (json.dumps(args, indent=2) + '\n').encode('utf-8'))
            result['merge_results'].append({'path': name, 'actual_exit': merged.returncode,
                                            'raw_output_sha256': digest(merged.stdout)})
        else:
            assert name not in mg_before or original[name] == mg_before[name]
            save(candidate / name, incoming.read_bytes())
    for name, expected in lis_sources.items():
        # 作者候选使用同一稀疏目录布局，逐份确认原前像后才复制批准后像。
        candidates = [LIS / 'candidate/06_programs/sn100_host' / name, LIS / 'candidate' / name]
        incoming = next(path for path in candidates if path.is_file())
        bases = [LIS / 'base/06_programs/sn100_host' / name, LIS / 'base' / name]
        baseline = next(path for path in bases if path.is_file())
        assert digest(incoming.read_bytes()) == expected and digest(baseline.read_bytes()) == original[name]
        save(candidate / name, incoming.read_bytes())
    raw = read_at(PROGRAM, 'reagents/reagents.pri')
    needle = b'HEADERS *= $$PWD/original_reagent_page_metadata.h'
    assert raw.count(needle) == 1
    save(candidate / 'reagents/reagents.pri', raw.replace(needle, needle + b' $$PWD/original_reagent_probe.h'))
    raw = read_at(PROGRAM, 'shell/build.sh')
    needle = b'    "$program_dir/reagents/original_reagent_page_metadata.h"'
    assert raw.count(needle) == 1
    save(candidate / 'shell/build.sh', raw.replace(needle, needle + b'\n    "$program_dir/reagents/original_reagent_probe.h"'))
    result['sparse_candidate_files'] = sum(1 for path in candidate.rglob('*') if path.is_file())
    result['unresolved_merge_files'] = [item['path'] for item in result['merge_results'] if item['actual_exit']]
    save(OUT / 'preparation.json', (json.dumps(result, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
