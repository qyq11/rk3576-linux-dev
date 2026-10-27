"""Add actual collector receipts and read back the entire text-only archive."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[4]
PREP = Path(__file__).resolve().parent
TARGET = ROOT / '04_references/sn100_host/local_features_integration_20261026/native'


def require(value, message):
    if not value:
        raise RuntimeError(message)


def checked(path):
    data = path.read_bytes()
    require('\x00' not in data.decode('utf-8', errors='strict'), 'Forbidden NUL: ' + str(path))
    return data


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    initial = checked(TARGET / 'SHA256SUMS')
    for line in initial.decode('utf-8').splitlines():
        expected, name = line.split('  ', 1)
        require(sha(checked(TARGET / name)) == expected, 'Initial archive changed')
    receipt = json.loads(checked(PREP / 'exit.json'))
    command = json.loads(checked(PREP / 'command.json'))
    require(receipt['actual_exit'] == 0 and command['collector_sha256']
            == receipt['collector_sha256_after'] == sha(checked(PREP / 'archive_native.py')),
            'Actual archive collector process or source does not agree')
    require(command['AST_actual_exit'] == 0, 'Collector AST receipt failed')
    destination = TARGET / 'collector_execution'
    destination.mkdir()
    index = []
    inputs = [(PREP / name, name) for name in ('command.json', 'exit.json', 'stdout.log', 'stderr.log')]
    inputs.append((Path(__file__), 'finalize_archive.py'))
    for source, name in inputs:
        data = checked(source)
        target = destination / name
        with target.open('xb') as stream:
            stream.write(data)
        require(checked(target) == data == checked(source), 'Supplementary receipt readback mismatch')
        index.append({'source': source.relative_to(ROOT).as_posix(),
                      'archive': target.relative_to(TARGET).as_posix(),
                      'bytes': len(data), 'sha256': sha(data)})
    with (destination / 'initial_SHA256SUMS').open('xb') as stream:
        stream.write(initial)
    summary = {'schema': 'sn100-native-text-archive-finalization-v1',
        'initial_archive_SHA256SUMS_sha256': sha(initial),
        'actual_collector_exit': receipt['actual_exit'], 'collector_AST_exit': command['AST_actual_exit'],
        'supplementary_inputs': index, 'strict_UTF8_no_NUL': True,
        'current_business_or_build_rerun': False, 'object_program_or_runtime_read': False,
        'initial_handoff_and_manifest_retained_byte_for_byte': True,
        'final_index': 'SHA256SUMS covers all final files except itself; initial index retained separately'}
    data = (json.dumps(summary, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    with (destination / 'finalization.json').open('xb') as stream:
        stream.write(data)
    rows = []
    for path in sorted(TARGET.rglob('*')):
        if path.is_file() and path.name != 'SHA256SUMS':
            data = checked(path)
            rows.append({'path': path.relative_to(TARGET).as_posix(), 'bytes': len(data), 'sha256': sha(data)})
    sums = ''.join(row['sha256'] + '  ' + row['path'] + '\n' for row in rows).encode('utf-8')
    (TARGET / 'SHA256SUMS').write_bytes(sums)
    for row in rows:
        data = checked(TARGET / row['path'])
        require(len(data) == row['bytes'] and sha(data) == row['sha256'], 'Final length/SHA readback failed')
    require(checked(TARGET / 'SHA256SUMS') == sums, 'Final SUMS byte readback failed')
    print(json.dumps({'actual_collector_exit': receipt['actual_exit'],
        'original_copied_files': len(json.loads(checked(TARGET / 'copied_inputs.json'))),
        'final_files_including_SUMS': len(rows) + 1, 'SHA_index_entries': len(rows),
        'handoff_sha256': sha(checked(TARGET / 'handoff.json')),
        'SHA256SUMS_sha256': sha(sums), 'final_readback_all_equal': True}))


if __name__ == '__main__':
    main()
