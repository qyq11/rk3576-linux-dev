#!/usr/bin/env python3
"""Archive only explicit nonbinary peer-gate evidence; no target/build execution or runtime traversal."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re


STDERR_SHA = 'e816706cd44ce0c6c65a7a7938be8103514f7c899694c8240d844f74319b7f98'
STDERR_BYTES = 153


def digest(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    args = parser.parse_args()
    require(args.root_go, 'Explicit archive authorization required')
    source = Path(__file__).resolve().parent
    repo = source.parents[3]
    original = source / 'run_01'
    revalidation = source / 'revalidation_01'
    preimage = repo / '07_backups/sn100_host/offline_followup_20261026/protocol_peer_gate_preimage'
    output = repo / '04_references/sn100_host/offline_followup_20261026/protocol_peer_gate'
    require(not output.exists() and not output.is_symlink(), 'Never overwrite an existing archive')
    run_verification = read_json(original / 'verification.json')
    revalidated = read_json(revalidation / 'verification.json')
    launch_exit = read_json(revalidation / 'launch.exit.json')
    root_observation = read_json(original / 'root_parent_observation.json')
    require(run_verification['passed'] is False and revalidated['passed'] is True
            and revalidated['original_evidence_before_after_equal'] is True
            and revalidated['bound_input_count'] == 56 and launch_exit['actual_exit_code'] == 0
            and root_observation['actual_parent_exit_from_tool'] == 1
            and root_observation['recorded_after_completion'] is True,
            'Original failure/read-only revalidation outcome differs')
    original_inputs = read_json(original / 'inputs_before.json')
    require(original_inputs == read_json(original / 'inputs_after.json')
            == read_json(original / 'preparation_executed.json')['source_inputs'] and len(original_inputs) == 14,
            'Original fourteen-input binding differs')
    duration = re.findall(r'Totals: 5 passed, 0 failed, 0 skipped, 0 blacklisted, (\d+)ms',
                          (original / 'qt.txt').read_text(encoding='utf-8'))
    require(len(duration) == 1, 'Original Qt elapsed receipt missing')
    future = read_json(source / 'preparation.json')
    require(len(future['source_inputs']) == 14, 'Future input roster differs')
    for relative, expected in future['source_inputs'].items():
        require(digest((repo / relative).read_bytes()) == expected, 'Future preparation input changed: ' + relative)
    selected = {}
    root_names = ['artifact.json', 'inputs_before.json', 'inputs_after.json', 'preparation_executed.json',
                  'qt.txt', 'qt.xml', 'verification.json', 'root_parent_observation.json']
    for stage in ('qmake', 'make', 'test'):
        root_names.extend(stage + suffix for suffix in ('.command.json', '.exit.json', '.exit', '.log', '.process.json'))
    require({p.name for p in original.iterdir() if p.is_file()} == set(root_names), 'Original root roster differs')
    for name in root_names:
        selected['run_01/' + name] = original / name
    for name in ('baseline', 'peer_read_gate', 'peer_reply_gate'):
        selected['run_01/evidence/' + name + '.json'] = original / 'evidence' / (name + '.json')
    for relative, expected in original_inputs.items():
        path = original / 'executed_sources' / relative
        require(digest(path.read_bytes()) == expected, 'Executed source changed: ' + relative)
        selected['run_01/executed_sources/' + relative] = path
    revalidation_names = ('inputs_before.json', 'inputs_after.json', 'case_validation.json',
                         'observed_timelines.json', 'verification.json', 'launch.command.json',
                         'launch.stdout.log', 'launch.stderr.log', 'launch.exit.json')
    require({p.name for p in revalidation.iterdir()} == set(revalidation_names), 'Revalidation roster differs')
    for name in revalidation_names:
        selected['revalidation_01/' + name] = revalidation / name
    for name in ('protocol_peer_gate_test.cpp', 'protocol_peer_gate_test.pro', 'experiment_plan.json',
                 'run_peer_gate.py', 'preparation.json', 'revalidate_peer_gate.py', 'archive_peer_gate.py'):
        selected['future_source/' + name] = source / name
    for name in ('run_peer_gate.py', 'preparation.json', 'manifest.json'):
        selected['verifier_preimage/' + name] = preimage / name
    preimage_manifest = read_json(preimage / 'manifest.json')
    for name, expected in preimage_manifest['files'].items():
        require(digest((preimage / name).read_bytes()) == expected, 'Precise verifier preimage changed')

    # All original run/revalidator inputs that refer to immutable evidence remain exact.
    original_bound = read_json(revalidation / 'inputs_before.json')
    require(original_bound == read_json(revalidation / 'inputs_after.json'), 'Revalidation source binding differs')
    for relative, path in selected.items():
        if relative.startswith('run_01/'):
            require(digest(path.read_bytes()) == original_bound[relative], 'Original evidence changed after validation: ' + relative)
    validator_sha = digest((source / 'revalidate_peer_gate.py').read_bytes())
    require(validator_sha == original_bound['revalidator/revalidate_peer_gate.py']
            == launch_exit['revalidator_sha256'], 'Actually executed revalidator source changed')
    blobs = {}
    copied = {}
    for relative, path in sorted(selected.items()):
        require(path.is_file() and not path.is_symlink() and path.resolve(strict=True).is_relative_to(repo), 'Unsafe source')
        require(not any(part in ('build', 'runtime', '.obj', '.moc', 'sysroot') for part in Path(relative).parts), 'Forbidden tree')
        require(path.suffix in ('.cpp', '.h', '.pro', '.py', '.json', '.log', '.txt', '.xml', '.exit'), 'Unexpected extension')
        data = path.read_bytes()
        source_sha = digest(data)
        archived_relative = relative
        transform = None
        if relative == 'revalidation_01/launch.stderr.log':
            require(len(data) == STDERR_BYTES and source_sha == STDERR_SHA and b'\x00' in data,
                    'Only the specifically reviewed153-byte WSL capture may use a byte envelope')
            archived_relative += '.bytes.json'
            envelope = {'schema': 'sn100-lossless-captured-byte-envelope-v1',
                        'original_path': str(path), 'original_repository_relative_path': path.relative_to(repo).as_posix(),
                        'original_bytes': len(data), 'original_sha256': source_sha,
                        'encoding': 'base64', 'base64': base64.b64encode(data).decode('ascii'),
                        'capture_note': 'Raw WSL startup stderr contains NUL/mixed capture bytes. Preserve exact captured bytes; no recovery of original WSL Unicode is claimed.',
                        'original_raw_log_retained_in_temp': True}
            require(digest(base64.b64decode(envelope['base64'], validate=True)) == source_sha, 'Envelope roundtrip differs')
            data = json_bytes(envelope)
            transform = 'lossless_base64_byte_envelope_for_one_explicit_SHA_and_length'
        else:
            require(b'\x00' not in data, 'Unexpected binary/NUL text input: ' + relative)
            data.decode('utf-8', errors='strict')
        blobs[archived_relative] = data
        copied[relative] = {'source': str(path), 'source_sha256': source_sha, 'source_bytes': path.stat().st_size,
                            'archive_path': archived_relative, 'archive_sha256': digest(data),
                            'archive_bytes': len(data), 'transformation': transform}
    handoff = {'schema': 'sn100-protocol-peer-gate-followup-v1',
        'status': 'original_target_passed_parent_verifier_failed_readonly_revalidation_passed',
        'original_actual_qmake_make_test_exits': [read_json(original / (stage + '.exit.json'))['actual_exit_code']
                                                 for stage in ('qmake', 'make', 'test')],
        'original_qttest_totals': revalidated['qt_text_totals'],
        'original_qttest_elapsed_ms': int(duration[0]), 'original_parent_exit': root_observation['actual_parent_exit_from_tool'],
        'original_parent_exit_evidence': 'run_01/root_parent_observation.json: retrospective root tool observation, not a process-emitted receipt',
        'original_run_verification_passed': False, 'original_failure_preserved': True,
        'revalidation_actual_exit': launch_exit['actual_exit_code'], 'revalidation_passed': revalidated['passed'],
        'revalidation_bound_inputs': 56, 'revalidation_inputs_before_after_equal': True,
        'revalidation_reused_executed_driver_sha256': revalidated['executed_old_driver_sha256'],
        'qt5_junit_contract': 'Exactly three passing method groups; Qt text independently proves three data rows once each plus init/cleanup,5/0/0/0',
        'case_results': revalidated['case_results'], 'build_or_target_rerun_for_verifier_repair': False,
        'formal_source_modified': False, 'production_hook_added': False, 'old8h_direct_cause_confirmed': False,
        'old8h_fixed_claimed': False, 'future_driver_executed_after_correction': False,
        'future_driver_sha256': digest((source / 'run_peer_gate.py').read_bytes()),
        'future_preparation_sha256': digest((source / 'preparation.json').read_bytes()),
        'post_revalidation_temp_changes': ['run_peer_gate.py', 'preparation.json'],
        'source_binding_note': 'The revalidation current/ hashes record the moment before the later two-file verifier correction. Historical run evidence and C++ remain unchanged; precise old temp inputs are retained in07. No new whole-app freeze or QC result is inferred.',
        'preimage_manifest': '07_backups/sn100_host/offline_followup_20261026/protocol_peer_gate_preimage/manifest.json',
        'mechanism_observed': ['baseline returns one true terminal',
            'read gate:13bytes in peer Qt receive buffer at original false terminal; consumed/replied only after release',
            'reply gate:two requests already decoded but reply held until original false terminal',
            'negative arms:one late valid observation, still one false terminal and two attempts throughout250ms'],
        'limits': ['Synthetic anonymous localhost Qt mechanism; bytesWritten is not peer/TCP ACK',
            'Observer case/attempt labels are not online transaction IDs',
            'No new GUI/RK/instrument/long-run/resource qualification; old1150second direct cause remains unknown'],
        'database_ini_secret_object_binary_copied': False, 'copied_file_count': len(copied), 'copied_inputs': copied}
    output.mkdir()
    for relative, data in blobs.items():
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open('xb') as stream:
            stream.write(data)
    with (output / 'handoff.json').open('xb') as stream:
        stream.write(json_bytes(handoff))
    for relative, record in copied.items():
        require(digest(selected[relative].read_bytes()) == record['source_sha256'], 'Source changed during archive')
        require(digest((output / record['archive_path']).read_bytes()) == record['archive_sha256'], 'Archive readback differs')
    hashes = {path.relative_to(output).as_posix(): digest(path.read_bytes()) for path in output.rglob('*') if path.is_file()}
    with (output / 'SHA256SUMS').open('x', encoding='ascii') as stream:
        stream.write(''.join(value + '  ' + relative + '\n' for relative, value in sorted(hashes.items())))
    for relative, expected in hashes.items():
        require(digest((output / relative).read_bytes()) == expected, 'SHA readback differs')
    require(len(list(output.rglob('*.db'))) == 0, 'Unexpected runtime database')
    print(json.dumps({'archive': str(output), 'copied_files': len(copied), 'hashed_files': len(hashes),
                      'handoff_sha256': digest((output / 'handoff.json').read_bytes()),
                      'sha256sums_sha256': digest((output / 'SHA256SUMS').read_bytes()),
                      'readback_all_equal': True, 'original_parent_exit': 1, 'revalidation_actual_exit': 0}, sort_keys=True))


if __name__ == '__main__':
    main()
