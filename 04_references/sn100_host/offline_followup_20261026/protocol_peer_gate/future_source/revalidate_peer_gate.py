#!/usr/bin/env python3
"""Read-only revalidation of the one existing peer-gate run; never build or execute its target."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


OLD_DRIVER_SHA = '2d2b92f533706ba82fc5877ed76fd3b8115f2a72320e9e3c4dfc030d0656b264'
OLD_PREPARATION_SHA = '0f2ec9c7b954c182684f67da00c29c1efda515b265c5e208829c99249d532340'
ROOT_OBSERVATION_SHA = '9f9f9286754fff3af3abaec490ae05fe1cb0f3cc94bda61adde1abbcf577f686'
CASES = ('baseline', 'peer_read_gate', 'peer_reply_gate')
PRIVATE = 'temp/sn100_host/offline_followup_20261026/protocol_peer_gate/'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def save(path, value):
    with Path(path).open('x', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    require(os.name == 'posix' and os.geteuid() != 0, 'Run only as an ordinary WSL user')
    source = Path(__file__).resolve().parent
    repo = source.parents[3]
    run = args.run_dir
    output = args.output_dir
    require(run.is_absolute() and run.resolve(strict=True) == source / 'run_01', 'Only the preserved run_01 is supported')
    require(output.is_absolute() and not output.exists() and not output.is_symlink()
            and output.parent.resolve(strict=True) == source, 'Use a new direct-child revalidation directory')
    original_inputs = read_json(run / 'inputs_before.json')
    require(isinstance(original_inputs, dict) and len(original_inputs) == 14, 'Expected fourteen recorded inputs')
    top = ['artifact.json', 'inputs_before.json', 'inputs_after.json', 'preparation_executed.json',
           'qt.txt', 'qt.xml', 'verification.json', 'root_parent_observation.json']
    for stage in ('qmake', 'make', 'test'):
        top.extend(stage + suffix for suffix in ('.command.json', '.exit.json', '.exit', '.log', '.process.json'))
    require({p.name for p in run.iterdir() if p.is_file()} == set(top), 'Unexpected/missing run_01 root evidence file')
    require({p.name for p in (run / 'evidence').iterdir()} == {name + '.json' for name in CASES},
            'Unexpected/missing original case raw evidence')
    input_paths = {'run_01/' + name: run / name for name in top}
    input_paths.update({'run_01/evidence/' + name + '.json': run / 'evidence' / (name + '.json') for name in CASES})
    for relative, expected in original_inputs.items():
        require(not Path(relative).is_absolute() and '..' not in Path(relative).parts, 'Unsafe recorded input path')
        snapshot = run / 'executed_sources' / relative
        current = repo / relative
        require(snapshot.is_file() and not snapshot.is_symlink() and sha(snapshot) == expected,
                'Executed source snapshot differs: ' + relative)
        require(current.is_file() and not current.is_symlink() and sha(current) == expected,
                'Current recorded input changed before revalidation: ' + relative)
        input_paths['run_01/executed_sources/' + relative] = snapshot
        input_paths['current/' + relative] = current
    input_paths['current/preparation.json'] = source / 'preparation.json'
    input_paths['revalidator/revalidate_peer_gate.py'] = Path(__file__).resolve()
    require(all(path.is_file() and not path.is_symlink() and path.resolve(strict=True).is_relative_to(repo)
                for path in input_paths.values()), 'All inputs must be ordinary workspace files')
    before = {name: sha(path) for name, path in sorted(input_paths.items())}
    output.mkdir()
    save(output / 'inputs_before.json', before)
    passed = False
    failure = None
    cases = []
    result = {}
    try:
        old_driver = run / 'executed_sources' / (PRIVATE + 'run_peer_gate.py')
        require(sha(old_driver) == OLD_DRIVER_SHA and original_inputs[PRIVATE + 'run_peer_gate.py'] == OLD_DRIVER_SHA,
                'Recorded validator must be the exact executed old driver')
        require(sha(run / 'preparation_executed.json') == OLD_PREPARATION_SHA
                and sha(source / 'preparation.json') == OLD_PREPARATION_SHA, 'Executed preparation differs')
        preparation = read_json(run / 'preparation_executed.json')
        require(original_inputs == read_json(run / 'inputs_after.json') == preparation['source_inputs'],
                'Original fourteen inputs did not remain equal')
        original = read_json(run / 'verification.json')
        require(original.get('passed') is False and original.get('exception_type') == 'RuntimeError'
                and original.get('exception') == 'Raw JUnit XML has missing cases/failure/error/skip'
                and original.get('stages_completed') == ['qmake', 'make', 'test']
                and original.get('source_before_after_equal') is True
                and original.get('preparation_before_after_equal') is True
                and original.get('preparation_sha256') == OLD_PREPARATION_SHA
                and original.get('preparation_after_sha256') == OLD_PREPARATION_SHA,
                'Original failure/binding was changed or was not the identified XML grouping error')
        observation = read_json(run / 'root_parent_observation.json')
        require(sha(run / 'root_parent_observation.json') == ROOT_OBSERVATION_SHA
                and observation.get('recorded_after_completion') is True
                and observation.get('actual_parent_exit_from_tool') == 1
                and observation.get('schema') == 'sn100-root-tool-parent-observation-v1',
                'Root retrospective parent observation differs')
        receipts = {}
        for stage in ('qmake', 'make', 'test'):
            command = read_json(run / (stage + '.command.json'))
            exit_receipt = read_json(run / (stage + '.exit.json'))
            process = read_json(run / (stage + '.process.json'))
            require(all(exit_receipt.get(key) == value for key, value in command.items()), 'Command/exit relation differs: ' + stage)
            require(exit_receipt.get('actual_exit_code') == 0 and exit_receipt.get('direct_exit_before_cleanup') == 0
                    and exit_receipt.get('timed_out') is False and exit_receipt.get('owned_group_stopped') is True
                    and exit_receipt.get('exception') is None and exit_receipt.get('exception_type') is None
                    and (run / (stage + '.exit')).read_text(encoding='ascii') == '0\n',
                    'Actual original child did not complete normally: ' + stage)
            require(process.get('pid', 0) > 0 and process.get('owned_pgid') == process['pid']
                    and process.get('start_new_session') is True, 'Original owned group evidence differs: ' + stage)
            receipts[stage] = {'actual_exit_code': 0, 'owned_group_stopped': True,
                               'original_receipt_sha256': sha(run / (stage + '.exit.json'))}
        artifact = read_json(run / 'artifact.json')
        test_command = read_json(run / 'test.command.json')
        require(test_command.get('argv') == [artifact['path'], '-o', str(run / 'qt.txt') + ',txt',
                                             '-o', str(run / 'qt.xml') + ',junitxml']
                and test_command.get('explicit_environment') == {'SN100_PROTOCOL_PEER_GATE_OUTPUT': str(run / 'evidence')}
                and re.fullmatch('[a-f0-9]{64}', artifact.get('sha256', '')) is not None and artifact.get('bytes', 0) > 0,
                'Original target/output/artifact binding differs')
        text = (run / 'qt.txt').read_text(encoding='utf-8')
        totals = re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped, (\d+) blacklisted', text)
        require(totals == [('5', '0', '0', '0')], 'Original Qt text lacks exact5/0/0/0 totals')
        pass_tags = re.findall(r'PASS\s*:\s*ProtocolPeerGateTest::([^\r\n]+)', text)
        require(pass_tags == ['initTestCase()', 'peerGates(baseline)', 'peerGates(peer_read_gate)',
                              'peerGates(peer_reply_gate)', 'cleanupTestCase()'],
                'Original text must prove each data arm exactly once plus init/cleanup')
        require(re.search(r'^(?:FAIL!|SKIP|XFAIL|XPASS)\s*:', text, re.MULTILINE) is None,
                'Original Qt text contains failure/skip/expected-failure test outcomes')
        xml = ET.parse(run / 'qt.xml').getroot()
        grouped = list(xml.findall('testcase'))
        require(xml.tag == 'testsuite' and xml.get('name') == 'ProtocolPeerGateTest'
                and xml.get('tests') == '3' and xml.get('errors') == '0' and xml.get('failures') == '0'
                and xml.get('skipped', '0') == '0'
                and [row.get('name') for row in grouped] == ['initTestCase', 'peerGates', 'cleanupTestCase']
                and all(row.get('result') == 'pass' for row in grouped)
                and len(list(xml.iter('testcase'))) == 3
                and not any(list(xml.iter(tag)) for tag in ('failure', 'error', 'skipped')),
                'Original Qt5 grouped JUnit must contain exactly three passing methods, no failure/error/skip')
        # SHA-verified old file is loaded under a non-main name. Its main()/execute() are never called.
        old_text = old_driver.read_text(encoding='utf-8')
        old_ast = ast.parse(old_text, filename=str(old_driver))
        validate_node = next(node for node in old_ast.body if isinstance(node, ast.FunctionDef) and node.name == 'validate_case')
        validate_source = ast.get_source_segment(old_text, validate_node)
        namespace = {'__name__': '_recorded_peer_gate_validation_only_', '__file__': str(old_driver)}
        exec(compile(old_ast, str(old_driver), 'exec'), namespace)
        require(set(original_inputs) == namespace['REQUIRED'] and tuple(namespace['CASES']) == CASES,
                'Recorded driver input/case roster differs')
        cases = [namespace['validate_case'](run / 'evidence' / (name + '.json'), name) for name in CASES]
        save(output / 'case_validation.json', cases)
        timelines = []
        for name in CASES:
            raw = read_json(run / 'evidence' / (name + '.json'))
            stages = ('client.query.queued', 'transport.write.returned', 'client.socket.progress.not_peer_ack',
                      'peer.readyRead.qt_buffered', 'peer.read.gate.held', 'peer.read.actual', 'peer.frame.decoded',
                      'peer.reply.gate.held', 'client.request.failure', 'client.request.success', 'driver.verdict.latched',
                      'peer.gate.release', 'peer.reply.write.begin', 'peer.reply.write.returned',
                      'client.wire.read', 'client.observed', 'driver.postmortem.begin', 'driver.postmortem.end',
                      'driver.client.close.begin', 'driver.client.close.end', 'driver.peer.abort.begin', 'driver.peer.abort.end')
            timelines.append({'case': name, 'actual_request_success': raw['request_success'],
                              'terminal_snapshot': raw['terminal_snapshot'],
                              'events': [row for row in raw['events'] if row['stage'] in stages]})
        save(output / 'observed_timelines.json', timelines)
        result = {'original_child_receipts': receipts, 'original_parent_exit': 1,
                  'original_parent_exit_source': 'Root retrospective tool observation, not a process-emitted independent receipt',
                  'original_run_verification_passed': False, 'xml_group_count': 3, 'xml_all_groups_pass': True,
                  'qt_text_totals': [5, 0, 0, 0], 'case_results': cases,
                  'executed_old_driver_sha256': OLD_DRIVER_SHA,
                  'reused_validate_case_source_sha256': hashlib.sha256(validate_source.encode('utf-8')).hexdigest(),
                  'recorded_driver_main_or_execute_called': False,
                  'binary_sha256_from_original_preexecution_receipt': artifact['sha256'],
                  'target_reexecuted': False, 'build_reexecuted': False}
        passed = True
    except BaseException as error:
        failure = error
        raise
    finally:
        after = {name: sha(path) if path.is_file() else None for name, path in sorted(input_paths.items())}
        unchanged = before == after
        save(output / 'inputs_after.json', after)
        save(output / 'verification.json', {'schema': 'sn100-protocol-peer-gate-revalidation-v1',
            'passed': passed and unchanged, 'original_evidence_before_after_equal': unchanged,
            'bound_input_count': len(before), 'revalidation_only': True,
            'target_or_build_launched_by_revalidator': False,
            'original_run_modified': False, 'original_failure_changed_to_pass': False,
            'old8h_direct_cause_confirmed': False, 'old8h_fixed_claimed': False,
            'scope': 'Validate original Qt text, Qt5 grouped JUnit and all three raw case invariants without a new target run',
            'exception_type': type(failure).__name__ if failure else None,
            'exception': str(failure) if failure else None, **result})
    require(unchanged, 'Original/revalidator inputs changed during read-only revalidation')
    print(json.dumps({'revalidation_passed': passed and unchanged, 'original_parent_exit': 1,
                      'original_verification_passed': False, 'case_count': len(cases),
                      'qt_text_totals': [5, 0, 0, 0], 'xml_method_groups': 3,
                      'original_inputs_unchanged': unchanged}, sort_keys=True))


if __name__ == '__main__':
    main()
