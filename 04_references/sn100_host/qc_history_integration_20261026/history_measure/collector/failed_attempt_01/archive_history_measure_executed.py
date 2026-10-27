#!/usr/bin/env python3
"""Archive existing history-page measurement metadata only; never rerun it."""
import argparse
import base64
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import traceback
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[4]
WORK = ROOT / 'temp/sn100_host/qc_history_integration_20261026'
PREP = WORK / 'qc_history_measure_preparation'
RUN = PREP / 'history_measure_01'
DEST = ROOT / '04_references/sn100_host/qc_history_integration_20261026/history_measure'
SELF = Path(__file__).resolve()
FREEZE_SHA = '1486686b182dc8d1da55c52b45c91ea72f1b8c41677676cedf16d72b3fc4f401'
NUL_SHA = 'e816706cd44ce0c6c65a7a7938be8103514f7c899694c8240d844f74319b7f98'


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def fingerprint(path):
    raw = path.read_bytes()
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def relative(path):
    return path.resolve().relative_to(ROOT).as_posix()


def jread(path):
    return json.loads(path.read_text(encoding='utf-8'))


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8', newline='\n') as output:
        json.dump(data, output, ensure_ascii=False, indent=2)
        output.write('\n')


def wsl_path(path):
    prefix = '/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/'
    require(path.startswith(prefix), 'Unexpected external source path')
    return ROOT / path[len(prefix):]


def safe_text(raw, source):
    require(b'\0' not in raw, 'NUL in ordinary text: ' + str(source))
    raw.decode('utf-8')


def text_inputs():
    # Only these exact roots/files, never build/runtime/host_environment or QM.
    stage_root = [
        'binding_proof.json', 'closed_storage_counts_and_bytes.json',
        'executed_native_inputs_declared.json', 'executed_preparation.json',
        'executed_protocol.json', 'invocation.json', 'native.Makefile',
        'native_after.json', 'native_before.json', 'verification.json',
    ]
    preparation = [
        'ast.command.json', 'ast.exit.json', 'ast.stdout.log', 'command_plan.json',
        'handoff.json', 'manifest.json', 'native_inputs_declared.json',
        'preparation.json', 'protocol.json', 'static_review.json',
    ]
    nested = {
        'compile_environment': ['environment.json'],
        'response': ['environment.json', 'qt.txt', 'qt.xml', 'recomputed_summary.json',
                     'response.command.json', 'response.exit', 'response.exit.json', 'response.log'],
        'response/evidence': ['response_metrics.json', 'response_partial.json'],
        'resource': ['environment.json', 'qt.txt', 'qt.xml', 'resource_observer.command.json',
                     'resource_observer.exit', 'resource_observer.exit.json', 'resource_observer.log'],
        'resource/evidence': ['resource_ready.json', 'resource_state.json', 'response_partial.json'],
        'resource/observation': ['process.log', 'ready_observed.json', 'samples.json',
                                 'sampling_completeness.json', 'test.command.json',
                                 'test.exit.json', 'test.process.json', 'window_summary.json'],
    }
    rows = [(RUN / name, 'run/' + name) for name in stage_root]
    rows += [(PREP / name, 'preparation/' + name) for name in preparation]
    for folder, names in nested.items():
        rows += [(RUN / folder / name, 'run/' + folder + '/' + name) for name in names]
    rows += [(WORK / 'native/verification.json', 'native/verification.json'),
             (WORK / 'native/final_before.json', 'native/final_before.json')]
    require(len({name for _, name in rows}) == len(rows), 'Duplicate destination')
    return rows


def source_inputs(rows):
    prep = jread(PREP / 'preparation.json')
    manifest = jread(PREP / 'manifest.json')
    require(prep['source_inputs'] == 447 and prep['production_link_objects'] == 177,
            'Unexpected preparation cohort')
    paths = {p for p, _ in rows} | {PREP / 'ast.stderr.log', SELF}
    for name, expected in manifest['members'].items():
        path = PREP / name
        require(fingerprint(path) == expected, 'Preparation manifest changed: ' + name)
        paths.add(path)
    for name, expected in prep['private_inputs'].items():
        path = PREP / name
        require(fingerprint(path)['sha256'] == expected, 'Private input changed: ' + name)
        paths.add(path)
    for name, expected in prep['external_source_inputs'].items():
        path = wsl_path(name)
        require(fingerprint(path)['sha256'] == expected, 'External source changed: ' + name)
        paths.add(path)
    # Four executed source images are fingerprinted, not copied into formal evidence.
    for name in ['executed_observe_loaded_history.py', 'executed_qc_history_gui_fixture.h',
                 'executed_qc_history_measure_test.cpp', 'executed_run_measure.py']:
        paths.add(RUN / name)
    return paths


def check_exit(row):
    require(row['actual_exit_code'] == 0 and row['direct_exit_before_cleanup'] == 0
            and row['timed_out'] is False and row['owned_group_stopped'] is True
            and row['exception'] is None and row['cleanup'] is None, 'Actual stage exit was not clean')


def validate():
    proof = jread(RUN / 'binding_proof.json')
    prep = jread(PREP / 'preparation.json')
    require(fingerprint(PREP / 'preparation.json')['sha256']
            == jread(RUN / 'verification.json')['preparation_sha256'], 'Executed preparation SHA differs')
    for name in ['preparation.json', 'protocol.json', 'native_inputs_declared.json',
                 'observe_loaded_history.py', 'qc_history_measure_test.cpp', 'run_measure.py']:
        require(fingerprint(PREP / name) == fingerprint(RUN / ('executed_' + name)),
                'Executed input image differs: ' + name)
    require(fingerprint(RUN / 'executed_qc_history_gui_fixture.h')['sha256'] == proof['fixture_sha256']
            == next(sha for name, sha in prep['external_source_inputs'].items()
                    if name.endswith('/qc_history_gui_fixture.h')), 'Executed fixture SHA differs')
    native = jread(WORK / 'native/verification.json')
    frozen = jread(WORK / 'native/final_before.json')
    before = jread(RUN / 'native_before.json')
    require(proof['native'] == before == jread(RUN / 'native_after.json'), 'Native binding changed')
    require(native['passed'] is True and native['actual_exit'] == 0
            and native['source_inputs'] == len(frozen) == 447
            and native['snapshot_sha256'] == FREEZE_SHA
            and fingerprint(WORK / 'native/final_before.json')['sha256'] == FREEZE_SHA,
            'Native/freeze metadata invalid')
    require(fingerprint(WORK / 'native/verification.json')['sha256'] == before['native_verification_sha256'],
            'Native verification SHA differs')
    require(before['snapshot_sha256'] == FREEZE_SHA and len(before['objects']) == 177,
            'Expected current177 production-object metadata')
    for name, row in before['objects'].items():
        key = name.split('/native/', 1)[1]
        fresh = native['fresh_objects'][key]
        require(fresh['created_after_start'] is True and fresh['sha256'] == row['object_sha256'],
                'Object metadata not paired to native: ' + key)
    require(proof['new_production_object_compiles'] == 0 and proof['reused_private_test_binary'] is False,
            'Unexpected production rebuild/private binary reuse')
    for phase in ['moc', 'compile', 'link']:
        check_exit(proof['commands'][phase])
    require(before['main_artifacts'] == native['artifacts'],
            'Main/helper artifact metadata differ')
    outcomes = {}
    for folder, method, prefix in [('response', 'measureHistoryResponse', 'response'),
                                    ('resource', 'observeLoadedHistory', 'resource_observer')]:
        base = RUN / folder
        actual = jread(base / (prefix + '.exit.json'))
        check_exit(actual)
        require(int((base / (prefix + '.exit')).read_text()) == 0, 'Raw .exit differs')
        command = jread(base / (prefix + '.command.json'))
        require(command['argv'] == actual['argv'] and command['cwd'] == actual['cwd'], 'Command/exit differ')
        xml = ET.parse(base / 'qt.xml').getroot()
        cases = xml.findall('testcase')
        require(xml.attrib['errors'] == '1' and xml.attrib['failures'] == '0' and xml.attrib['tests'] == '3',
                'Original Qt diagnostic attributes differ')
        require([q.attrib['name'] for q in cases] == ['initTestCase', method, 'cleanupTestCase']
                and all(q.attrib.get('result') == 'pass' for q in cases)
                and not xml.findall('.//failure') and not xml.findall('.//error')
                and not xml.findall('.//skipped'), 'Actual XML tests did not pass')
        text = (base / 'qt.txt').read_text(encoding='utf-8')
        require(re.search(r'Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted,', text), 'Qt raw totals differ')
        outcomes[folder] = {'testcases': 3, 'passed': 3, 'failed': 0, 'skipped': 0,
                            'original_xml_errors_attribute': xml.attrib['errors']}
    child = jread(RUN / 'resource/observation/test.exit.json')
    require(child['actual_exit_code'] == 0 and child['timed_out'] is False, 'Resource Qt child did not exit0')
    metrics = jread(RUN / 'response/evidence/response_metrics.json')
    require(metrics['schema'] == 'sn100-private-history-response-v1' and metrics['complete'] is True
            and metrics['errors'] == 0 and metrics['measured_click_queries'] == 40
            and metrics['setup_history_queries'] == 1 and metrics['total_history_queries'] == 41
            and metrics['worker_reply_count_total'] == 41 and len(metrics['samples']) == 40,
            'Actual response operation counts differ')
    for key in ['all_sql_rows_unchanged', 'history_unchanged', 'normal_page_leave', 'stores_closed', 'worker_joined']:
        require(metrics[key] is True, 'Response lifecycle/data check failed: ' + key)
    require(metrics['physical_input_or_compositor_completion'] is False, 'Unexpected physical endpoint claim')
    require([q['index'] for q in metrics['samples']] == list(range(1, 41))
            and all(q['success'] is True and q['unknown_units'] is True and q['valid_points'] == 3
                    for q in metrics['samples']), 'Response sample outcomes differ')
    delays = sorted(int(q['plot_paint_dispatch_return_observed_ns']) - int(q['queued_mouse_action_ns'])
                    for q in metrics['samples'])
    paint = {'count': len(delays), 'p95_ms': delays[math.ceil(.95 * len(delays)) - 1] / 1e6,
             'max_ms': delays[-1] / 1e6, 'endpoint': 'CPU plot Paint dispatch-return observation; not compositor/physical input'}
    summary = jread(RUN / 'response/recomputed_summary.json')['plot_paint_dispatch_return_observed_ns']
    require(summary['count'] == paint['count'] and summary['p95_ms'] == paint['p95_ms']
            and summary['max_ms'] == paint['max_ms'], 'Paint raw arithmetic differs')
    ready = jread(RUN / 'resource/evidence/resource_ready.json')
    state = jread(RUN / 'resource/evidence/resource_state.json')
    observed = jread(RUN / 'resource/observation/ready_observed.json')
    window = jread(RUN / 'resource/observation/window_summary.json')
    samples = jread(RUN / 'resource/observation/samples.json')
    completeness = jread(RUN / 'resource/observation/sampling_completeness.json')
    require(int(ready['deadline_ns']) - int(ready['ready_ns']) == 30_000_000_000
            and int(state['ready_ns']) == int(ready['ready_ns'])
            and int(state['deadline_ns']) == int(ready['deadline_ns'])
            and int(state['ended_ns']) >= int(ready['deadline_ns'])
            and observed['ready_ns'] == int(ready['ready_ns'])
            and observed['deadline_ns'] == int(ready['deadline_ns']), 'READY30s window differs')
    for key in ['complete', 'all_sql_rows_unchanged', 'history_unchanged', 'normal_page_leave', 'stores_closed', 'worker_joined']:
        require(state[key] is True, 'Resource lifecycle/data failed: ' + key)
    require(state['history_queries_total'] == 1 and ready['loaded_card_points'] == 3
            and ready['unknown_units'] is True and ready['rows'] == 6, 'Loaded-card scenario differs')
    window_samples = [row for row in samples
                      if int(ready['ready_ns']) <= row['monotonic_ns'] < int(ready['deadline_ns'])]
    require(completeness['complete'] is True and window['sampling_complete'] is True
            and window['sample_count'] == completeness['sample_count'] == len(window_samples), 'Sampling coverage differs')
    require(all(a['monotonic_ns'] < b['monotonic_ns'] for a, b in zip(samples, samples[1:])),
            'Resource raw sample order differs')
    first_gap = (window_samples[0]['monotonic_ns'] - int(ready['ready_ns'])) / 1e6
    last_gap = (int(ready['deadline_ns']) - window_samples[-1]['monotonic_ns']) / 1e6
    internal_gap = max(b['monotonic_ns'] - a['monotonic_ns']
                       for a, b in zip(window_samples, window_samples[1:])) / 1e6
    require(first_gap == completeness['first_sample_gap_ms']
            and last_gap == completeness['last_sample_gap_ms']
            and internal_gap == completeness['maximum_internal_gap_ms'], 'Resource raw sampling arithmetic differs')
    verification = jread(RUN / 'verification.json')
    require(verification['passed'] is True and verification['source_inputs'] == 447
            and verification['production_link_objects'] == 177
            and verification['native_before_after_equal'] is True
            and verification['response_samples'] == 40 and verification['setup_queries_per_process'] == 1
            and verification['resource_window_seconds'] == 30
            and verification['budget_pass'] is None and verification['whole_program_resource_pass'] is False,
            'Actual top verification differs')
    return {'native_source_inputs': len(frozen), 'native_objects_total': len(native['fresh_objects']),
            'private_inputs': len(prep['private_inputs']), 'external_source_inputs': len(prep['external_source_inputs']),
            'paired_production_objects_metadata_only': len(before['objects']),
            'production_object_or_binary_bytes_rehashed': False, 'qt': outcomes,
            'response_measured_actions': len(metrics['samples']), 'setup_queries': 1, 'total_queries': 41,
            'response_paint_dispatch_return': paint, 'resource_window_seconds_after_READY': 30,
            'resource_observation': window, 'sampling_completeness': completeness,
            'resource_raw_sample_count_including_pre_post_window': len(samples),
            'actual_stage_exit_codes': {'moc': 0, 'compile': 0, 'link': 0, 'response': 0, 'resource_observer': 0, 'resource_Qt_child': 0},
            'not_claimed': ['whole-program resource cost', 'approved numerical budget', 'physical input/compositor',
                            'Windows behavior', 'RK execution', 'instrument/clinical/realtime/long stability']}


def worker(attempt):
    require(not DEST.exists(), 'Formal destination already exists; never overwrite')
    rows = text_inputs()
    paths = source_inputs(rows)
    before = {relative(p): fingerprint(p) for p in sorted(paths)}
    summary = validate()
    for path, _ in rows:
        safe_text(path.read_bytes(), path)
    special = PREP / 'ast.stderr.log'
    raw = special.read_bytes()
    require(len(raw) == 153 and hashlib.sha256(raw).hexdigest() == NUL_SHA and b'\0' in raw,
            'Approved one-file captured-byte exception no longer matches')
    DEST.mkdir(parents=True, exist_ok=False)
    index = []
    for source, target in rows:
        output = DEST / target
        output.parent.mkdir(parents=True, exist_ok=True)
        data = source.read_bytes()
        with output.open('xb') as stream:
            stream.write(data)
        require(fingerprint(output) == before[relative(source)], 'Copy readback differs')
        index.append({'source': relative(source), 'destination': target, **fingerprint(output), 'copy': 'exact bytes'})
    envelope = {'schema': 'sn100-captured-byte-envelope-v1', 'source': relative(special),
                'original_bytes': len(raw), 'original_sha256': NUL_SHA,
                'captured_file_bytes_losslessly_preserved': True, 'upstream_pre_capture_fidelity': 'unknown',
                'capture_contains_nul': True, 'capture_contains_utf8_replacement_character': b'\xef\xbf\xbd' in raw,
                'original_OS_stderr_reconstructed': False, 'Unicode_recovery_claimed': False,
                'reason': 'Root approved only this exact153-byte/SHA preparation capture as a base64 envelope; ordinary text checks unchanged.',
                'encoding': 'base64', 'data': base64.b64encode(raw).decode('ascii')}
    target = 'preparation/ast.stderr.log.captured_bytes.json'
    write_json(DEST / target, envelope)
    require(base64.b64decode(jread(DEST / target)['data'], validate=True) == raw, 'Envelope is not lossless')
    index.append({'source': relative(special), 'source_bytes': len(raw), 'source_sha256': NUL_SHA,
                  'destination': target, **fingerprint(DEST / target), 'copy': 'lossless captured-byte envelope; upstream fidelity unknown'})
    excluded = []
    for path in sorted(paths):
        if path.suffix.lower() in {'.cpp', '.h', '.py'} and path != SELF:
            excluded.append({'source': relative(path), **before[relative(path)],
                             'reason': 'Source text deliberately not copied; byte SHA retained. No source redaction or modification.'})
    write_json(DEST / 'source_text_not_copied.json', excluded)
    write_json(DEST / 'input_fingerprints_before.json', before)
    after = {relative(p): fingerprint(p) for p in sorted(paths)}
    require(before == after, 'A read-only input changed while collecting')
    write_json(DEST / 'input_fingerprints_after.json', after)
    write_json(DEST / 'checked_actual_metadata.json', summary)
    write_json(DEST / 'measurement_parent_retrospective.json', {
        'schema': 'sn100-root-tool-observation-retrospective-v1', 'retrospective': True,
        'source': 'Root collaboration instruction for this archive', 'unified_exec_session': 13442,
        'completion_chunk': 'c4b543', 'observed_actual_parent_exit_code': 0,
        'independent_process_emitted_receipt': False, 'OS_PID': None,
        'full_parent_argv_not_captured': True, 'full_environment_not_reconstructed': True,
        'child_stage_receipts': 'Original stage receipts are retained independently; this note does not replace them.'})
    write_json(attempt / 'worker_result.json', {'passed': True, 'copied_inputs': index,
               'input_count': len(paths), 'excluded_source_texts': len(excluded), 'summary': summary})
    print(json.dumps({'metadata_collected': True, 'copied_inputs': len(index), 'input_count': len(paths)}))


def seal(attempt):
    result = jread(attempt / 'worker_result.json')
    actual = jread(attempt / 'collector.exit.json')
    require(actual['actual_exit_code'] == 0 and not actual['timed_out'], 'Metadata collector child failed')
    copied = result['copied_inputs']
    for name in ['collector.command.json', 'collector.process.json', 'collector.exit.json', 'collector.stdout.log', 'collector.stderr.log']:
        source = attempt / name
        raw = source.read_bytes()
        safe_text(raw, source)
        target = DEST / 'collector' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream:
            stream.write(raw)
        require(fingerprint(target) == fingerprint(source), 'Collector receipt readback differs')
        copied.append({'source': relative(source), 'destination': 'collector/' + name,
                       **fingerprint(target), 'copy': 'exact bytes'})
    target = DEST / 'collector/archive_history_measure.py'
    with target.open('xb') as stream:
        stream.write(SELF.read_bytes())
    require(fingerprint(target) == fingerprint(SELF), 'Collector source readback differs')
    copied.append({'source': relative(SELF), 'destination': 'collector/archive_history_measure.py',
                   **fingerprint(target), 'copy': 'exact bytes; collector source exception only'})
    write_json(DEST / 'copied_inputs.json', copied)
    write_json(DEST / 'handoff.json', {
        'schema': 'sn100-current447-history-page-measure-archive-v1', 'created_utc': now(),
        'status': 'Actual existing evidence archived; no measurement/build/test rerun',
        'source_attempt': relative(RUN), 'native_freeze_sha256': FREEZE_SHA,
        'summary': result['summary'], 'read_only_input_count': result['input_count'],
        'copied_input_files': len(copied), 'excluded_source_texts': result['excluded_source_texts'],
        'collector_worker_actual_exit_code': actual['actual_exit_code'],
        'measurement_parent': 'measurement_parent_retrospective.json; root session13442/chunkc4b543 actual0 is retrospective tool observation, not independent OS receipt',
        'original_prepare_status': 'Original preparation says not executed; preserved as its historical preparation state, not rewritten.',
        'capture_exception': 'Only exact153-byte/SHAe816 captured preparation stderr is enveloped; original temp file retained. No upstream Unicode/OS stderr reconstruction claim.',
        'boundaries': ['No build/runtime/host_environment/sysroot/DB/INI/auth/QM/PNG/executable/object copied',
                       'No C++/header/Python source text copied, except this metadata collector; source byte SHA retained',
                       'CPU approximately0.669% of one logical core belongs to this loaded-page sample only; no whole-program increment comparison',
                       'Resource sampling0.1s is a target including observer overhead; VmHWM is process-lifetime RSS high water',
                       'Performance/resource budget remains unknown; not physical/compositor/RK/instrument/clinical/realtime acceptance']})
    files = sorted(p for p in DEST.rglob('*') if p.is_file())
    initial = {p.relative_to(DEST).as_posix(): fingerprint(p) for p in files}
    require(all(fingerprint(DEST / name) == row for name, row in initial.items()), 'Archive first full readback differs')
    write_json(DEST / 'readback.json', {'schema': 'sn100-text-archive-readback-v1',
               'checked_files_before_this_record': len(initial), 'all_lengths_and_sha256_equal': True,
               'files': initial, 'does_not_hash_itself_or_SHA256SUMS': True})
    all_files = sorted(p for p in DEST.rglob('*') if p.is_file())
    checks = {p.relative_to(DEST).as_posix(): fingerprint(p) for p in all_files}
    with (DEST / 'SHA256SUMS').open('x', encoding='ascii', newline='\n') as stream:
        for name, row in checks.items():
            stream.write(row['sha256'] + '  ' + name + '\n')
    for line in (DEST / 'SHA256SUMS').read_text(encoding='ascii').splitlines():
        digest, name = line.split('  ', 1)
        require(fingerprint(DEST / name) == checks[name] and checks[name]['sha256'] == digest,
                'Final SHA256SUMS/length readback mismatch: ' + name)
    report = {'archive': relative(DEST), 'files': len(checks) + 1,
              'SHA256SUMS_entries': len(checks), 'copied_inputs': len(copied),
              'collector_worker_actual_exit': actual['actual_exit_code'],
              'handoff': fingerprint(DEST / 'handoff.json'), 'SHA256SUMS': fingerprint(DEST / 'SHA256SUMS')}
    write_json(attempt / 'completed.json', report)
    print(json.dumps(report, ensure_ascii=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', action='store_true')
    parser.add_argument('--attempt', default='history_measure_archive_01')
    args = parser.parse_args()
    require(re.fullmatch(r'history_measure_archive_\d{2}', args.attempt), 'Unsafe attempt name')
    attempt = WORK / 'archive_attempts' / args.attempt
    if args.worker:
        require(attempt.is_dir(), 'Worker attempt not initialized')
        worker(attempt)
        return 0
    require(not DEST.exists(), 'Archive already exists; refusing overwrite')
    attempt.mkdir(parents=True, exist_ok=False)
    argv = [sys.executable, '-B', str(SELF), '--worker', '--attempt', args.attempt]
    write_json(attempt / 'collector.command.json', {'argv': argv, 'cwd': str(ROOT), 'started_utc': now(),
               'scope': 'ordinary Python metadata copy/validation only; no business processes launched',
               'timeout_seconds': 120, 'outer_argv': sys.argv, 'outer_process_pid': os.getpid()})
    proc = None
    timed_out = False
    with (attempt / 'collector.stdout.log').open('xb') as stdout, (attempt / 'collector.stderr.log').open('xb') as stderr:
        try:
            proc = subprocess.Popen(argv, cwd=ROOT, stdout=stdout, stderr=stderr)
            write_json(attempt / 'collector.process.json', {'pid': proc.pid, 'started_utc': now(),
                       'role': 'metadata collector child only', 'emitted_by_parent_pid': os.getpid()})
            try:
                code = proc.wait(timeout=120)
            except subprocess.TimeoutExpired:
                timed_out = True
                proc.kill()
                code = proc.wait(timeout=10)
        except Exception:
            if proc is not None and proc.poll() is None:
                proc.kill()
                proc.wait(timeout=10)
            write_json(attempt / 'collector.exit.json', {'actual_exit_code': proc.returncode if proc else None,
                       'timed_out': timed_out, 'finished_utc': now(), 'exception': traceback.format_exc()})
            raise
    # Persist the real child exit before any success assertion or sealing.
    write_json(attempt / 'collector.exit.json', {'actual_exit_code': code, 'timed_out': timed_out,
               'finished_utc': now(), 'exception': None})
    require(code == 0 and not timed_out, 'Metadata collector failed; attempt/output retained')
    seal(attempt)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception:
        traceback.print_exc()
        raise SystemExit(1)
