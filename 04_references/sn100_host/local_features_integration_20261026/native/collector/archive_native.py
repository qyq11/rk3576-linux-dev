"""Archive completed native proof text only; never open build artifacts or data."""
from pathlib import Path
import datetime
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[4]
WORK = ROOT / 'temp/sn100_host/local_features_integration_20261026'
SOURCE = WORK / 'native'
LAUNCH = WORK / 'native_launch_01'
CONTROLLERS = WORK / 'integration_drivers'
TARGET = ROOT / '04_references/sn100_host/local_features_integration_20261026/native'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def text_bytes(path):
    data = path.read_bytes()
    text = data.decode('utf-8', errors='strict')
    require('\x00' not in text, 'NUL is forbidden in archived text: ' + str(path))
    return data


def read(path):
    return json.loads(text_bytes(path))


def save(path, value):
    data = (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    with path.open('xb') as stream:
        stream.write(data)
    require(path.read_bytes() == data, 'Generated metadata readback differs')


def main():
    require(not TARGET.exists(), 'Fresh unique archive target required; retain failed attempts')
    proof = read(SOURCE / 'verification.json')
    frozen = read(SOURCE / 'final_before.json')
    frozen_after = read(SOURCE / 'final_after.json')
    require(proof['schema'] == 'sn100-local-features-native-release-v1'
            and proof['passed'] is True and proof['actual_exit'] == 0,
            'Successful current native producer proof required')
    require(isinstance(frozen, dict) and len(frozen) == proof['source_inputs'] == 478
            and frozen == frozen_after and proof['source_before_after_equal'] is True,
            'Current root-observed 478 source metadata must agree')
    require(digest(text_bytes(SOURCE / 'final_before.json')) == proof['snapshot_sha256']
            == '6c22e2017b1afc06afdd6bb8d2fd753e1d8b7b2f1c35c976cb38329a3c66faa8',
            'Source snapshot receipt mismatch')
    require(proof['parallel_jobs_maximum'] == 2, 'Native jobs bound differs')
    objects = proof['fresh_objects']
    require(isinstance(objects, dict) and len(objects) == proof['fresh_object_count'] == 226,
            'Fresh-object producer metadata must match the completed current build')
    counts = {'main': 0, 'renderer': 0}
    for name, row in objects.items():
        require(re.fullmatch(r'(?:wsl_release|wsl_renderer_release)/\.obj/[^/]+\.o', name)
                and row['created_after_start'] is True and re.fullmatch(r'[a-f0-9]{64}', row['sha256']),
                'Malformed producer object metadata')
        counts['renderer' if name.startswith('wsl_renderer_release/') else 'main'] += 1
    require(counts == {'main': 194, 'renderer': 32}, 'Current producer object role metadata differs')
    require(proof['artifacts'] == {
        'sn100_shell': 'bcf67364fb33578e3517a84943eabc87bc9650e08fb463bb49f3bb941e322253',
        'sn100_report_renderer': '2de67df76a6871884e52584cefb613fa89daa657a59938435f61f4bd875fe7eb'},
        'Current producer main/helper metadata differs from root observation')
    stages = ('snapshot_before', 'build', 'snapshot_after', 'artifact_types')
    stage_summary = {}
    for stage in stages:
        command = read(SOURCE / (stage + '.command.json'))
        raw = read(SOURCE / (stage + '.raw_exit.json'))
        receipt = read(SOURCE / (stage + '.exit.json'))
        require(raw['exit_code'] == receipt['exit_code'] == 0
                and receipt['timed_out'] is False and receipt['state'] == 'finished'
                and receipt['owned_process_group_stopped'] is True
                and receipt['group_after']['gone'] is True
                and receipt['cleanup_errors'] == [] and receipt['cleanup'] is None,
                'Actual native stage failed, timed out or needed cleanup: ' + stage)
        require(raw['pid'] == receipt['pid']
                and command['command'] == receipt['command']
                and text_bytes(SOURCE / (stage + '.exit')).strip() == b'0',
                'Native original command/exit metadata does not agree: ' + stage)
        stage_summary[stage] = {'actual_exit': receipt['exit_code'],
            'owned_group_stopped': receipt['owned_process_group_stopped'],
            'timed_out': receipt['timed_out'], 'cleanup': receipt['cleanup']}
    control_before = read(SOURCE / 'controller_inputs_before.json')
    control_after = read(SOURCE / 'controller_inputs_after.json')
    prep = read(CONTROLLERS / 'preparation.json')
    require(control_before == control_after == prep['controller_inputs']
            and proof['controller_inputs_unchanged'] is True, 'Controller input receipts differ')
    launch_command = read(LAUNCH / 'command.json')
    launch_exit = read(LAUNCH / 'exit.json')
    require(launch_exit['actual_parent_exit'] == 0
            and launch_exit['controller_inputs_unchanged'] is True
            and launch_command['controller_inputs_before'] == control_before
            and launch_exit['controller_inputs_after'] == control_after
            and launch_command['preparation_sha'] == launch_exit['preparation_sha_after']
            == digest(text_bytes(CONTROLLERS / 'preparation.json')),
            'Original Windows launcher receipts do not bind current producer inputs')
    sources = []
    skipped = []
    for path in sorted(SOURCE.iterdir()):
        if path.is_file() and path.suffix in ('.json', '.log', '.exit'):
            sources.append((path, Path('native') / path.name))
        else:
            skipped.append({'path': path.relative_to(WORK).as_posix(),
                            'reason': 'Not an allowed top-level text receipt; never traversed'})
    for name in ('command.json', 'exit.json', 'windows_process.json'):
        sources.append((LAUNCH / name, Path('native_launch_01') / name))
    for name, expected in control_before.items():
        require(Path(name).name == name and name.endswith(('.py', '.json')),
                'Unexpected controller input name')
        path = CONTROLLERS / name
        require(digest(text_bytes(path)) == expected, 'Controller source input changed: ' + name)
        sources.append((path, Path('controller_inputs') / name))
    sources.append((CONTROLLERS / 'preparation.json', Path('controller_inputs/preparation.json')))
    sources.append((Path(__file__), Path('collector/archive_native.py')))
    # Strictly decode/check every allowed text before creating the formal target.
    before = [(source, target, text_bytes(source)) for source, target in sources]
    TARGET.mkdir(parents=True)
    copied = []
    for source, relative, data in before:
        target = TARGET / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream:
            stream.write(data)
        actual = target.read_bytes()
        require(len(actual) == len(data) and digest(actual) == digest(data)
                and source.read_bytes() == data, 'Original/copy readback changed: ' + str(relative))
        copied.append({'source': source.relative_to(ROOT).as_posix(),
            'archive': relative.as_posix(), 'bytes': len(data), 'sha256': digest(data)})
    save(TARGET / 'copied_inputs.json', copied)
    save(TARGET / 'root_parent_observation.json', {
        'retrospective_root_tool_observation': True, 'actual_parent_exit': 0,
        'unifiedexec_session': 31358, 'completion_chunk_id': 'edebfb',
        'producer_finished_utc': proof['finished_utc'],
        'source': 'Root internal collaboration report; separate original launcher command/exit retained',
        'is_independent_process_emitted_receipt': False,
        'OS_PID_or_full_environment_reconstructed': False})
    handoff = {'schema': 'sn100-local-features-native-text-archive-v1',
        'archived_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'source_inputs': len(frozen), 'snapshot_sha256': proof['snapshot_sha256'],
        'fresh_objects': len(objects), 'fresh_object_roles': counts,
        'actual_build_exit': proof['actual_exit'], 'main_artifacts': proof['artifacts'],
        'source_before_after_equal': True, 'controller_inputs_unchanged': True,
        'stage_receipts': stage_summary, 'original_copied_files': len(copied),
        'archive_scope': 'Completed WSL x86_64 Release compilation metadata/text only',
        'object_makefile_and_program_hashes': 'Copied producer metadata; not recomputed by archive collector',
        'not_read_or_copied': ['wsl_release', 'wsl_renderer_release', '.obj', '.moc', '.ui',
            'bin', 'runtime', 'databases/INI/authentication material',
            'native_launch_01/stdout.bytes', 'native_launch_01/stderr.bytes',
            'cumulative_build and its active logs'],
        'not_claimed': ['GUI or SQL behavior', 'Windows32', 'ARM64', 'RK board',
            'real instruments', 'clinical rules', 'runtime performance'],
        'skipped_top_level': skipped,
        'root_parent_observation': 'Retrospective tool fact; original launcher receipts also copied byte-for-byte',
        'strict_UTF8_and_no_NUL': True, 'binary_or_text_exception_added': False}
    save(TARGET / 'handoff.json', handoff)
    rows = []
    for path in sorted(TARGET.rglob('*')):
        if path.is_file():
            data = text_bytes(path)
            rows.append({'path': path.relative_to(TARGET).as_posix(), 'bytes': len(data), 'sha256': digest(data)})
    save(TARGET / 'archive_manifest.json', rows)
    path = TARGET / 'archive_manifest.json'
    data = text_bytes(path)
    rows.append({'path': path.name, 'bytes': len(data), 'sha256': digest(data)})
    sums = ''.join(row['sha256'] + '  ' + row['path'] + '\n' for row in sorted(rows, key=lambda r: r['path']))
    with (TARGET / 'SHA256SUMS').open('xb') as stream:
        stream.write(sums.encode('utf-8'))
    for row in rows:
        data = text_bytes(TARGET / row['path'])
        require(len(data) == row['bytes'] and digest(data) == row['sha256'], 'Final archive readback differs')
    print(json.dumps({'archive_completed': True, 'original_files': len(copied),
        'indexed_files': len(rows), 'total_files_including_SUMS': len(rows) + 1,
        'handoff_sha256': digest(text_bytes(TARGET / 'handoff.json')),
        'SHA256SUMS_sha256': digest(text_bytes(TARGET / 'SHA256SUMS'))}, ensure_ascii=False))


if __name__ == '__main__':
    main()
