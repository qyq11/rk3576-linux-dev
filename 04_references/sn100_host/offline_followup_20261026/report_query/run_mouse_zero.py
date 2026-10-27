"""Queued single XCB contrast. Reuse the verified driver; never compile/retry."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys
from native_binding import verify_native_binding

ROOT = Path(__file__).resolve().parents[4]
WORK = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute-slot', action='store_true')
    for name in ('output-dir', 'native-build', 'snapshot', 'native-verification', 'renderer', 'prior-run'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('snapshot-sha256', 'native-verification-sha256', 'driver-proof-sha256'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    if not args.execute_slot:
        parser.error('Wait for the integration window to allocate the single GUI slot')
    if not args.output_dir.is_absolute() or args.output_dir.exists():
        parser.error('A new absolute output directory is required; existing evidence is never overwritten')
    if not args.prior_run.is_absolute():
        parser.error('Explicit absolute prior-run path required')
    helpers = ROOT / 'temp/sn100_host/qc_local_20261025/integration_drivers/common.py'
    spec = importlib.util.spec_from_file_location('mouse_zero_evidence_helpers', helpers)
    c = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(c)
    args.output_dir.mkdir(parents=True)
    result = {'schema': 'sn100-report-query-single-mouse-zero-v1', 'started_utc': c.now(),
              'gui_invocations': 0, 'gui_invocation_attempts': 0, 'builds': 0, 'retries': 0,
              'variable': 'QtTest -mousedelay 0 only', 'historical_root_cause_proven': False,
              'production_or_formal_test_source_modified': False, 'slot_released': False}

    def verify():
        return verify_native_binding(program=ROOT / '06_programs/sn100_host',
            snapshot=args.snapshot, snapshot_sha256=args.snapshot_sha256,
            build=args.native_build, verification=args.native_verification,
            verification_sha256=args.native_verification_sha256, renderer=args.renderer)

    try:
        before = verify()
        c.save(args.output_dir / 'native_binding_before.json', before)
        proof_path = args.prior_run / 'build/binding_proof.json'
        c.require(c.sha(proof_path) == args.driver_proof_sha256, 'Prior driver proof SHA differs')
        proof = c.load(proof_path)
        c.require(proof['supplied_native_binding'] == before, 'Reused driver belongs to a different native binding')
        driver = proof['drivers']['integrated_report_test']
        binary = args.prior_run / 'build/integrated_report_test'
        c.require(c.sha(binary) == driver['binary_sha256'], 'Reused driver executable changed')
        prepared = c.load(WORK / 'prepared_sources.json')
        for path, expected in ((ROOT / '06_programs/sn100_host/tests/integrated_report_test.cpp', prepared['original_sha256']),
                               (WORK / 'source/integrated_report_test.cpp', driver['source_sha256']),
                               (WORK / 'report_query_trace.h', prepared['trace_header_sha256']),
                               (WORK / 'native_binding.py', prepared['native_binding_sha256'])):
            c.require(c.sha(path) == expected, 'Reviewed original/diagnostic source changed: ' + str(path))
        c.save(args.output_dir / 'reused_driver_inputs.json', {'proof_path': str(proof_path),
            'proof_sha256': c.sha(proof_path), 'binary': str(binary), 'driver': driver})
        selected = {'QT_QPA_PLATFORM': 'xcb',
            'SN100_INTEGRATED_REPORT_EVIDENCE_DIR': str(args.output_dir / 'data'),
            'SN100_LEGACY_QUERY_TRACE': str(args.output_dir / 'query_trace.jsonl'),
            'SN100_REPORT_RENDERER': str(args.renderer),
            'SN100_REPORT_PDF_PYTHON': '/mnt/c/Users/52400/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'}
        env = os.environ.copy()
        env.update(selected)
        c.save(args.output_dir / 'selected_environment.json', selected)
        command = [str(binary), 'committedHospitalAndReportLifecycle', 'persistedHospitalAndLegacyEntry',
                   '-mousedelay', '0', '-o', str(args.output_dir / 'qt.txt') + ',txt',
                   '-o', str(args.output_dir / 'qt.xml') + ',xml']
        result['gui_invocation_attempts'] = 1
        try:
            result['receipt'] = c.run_logged('test', command, args.output_dir, env=env, timeout=90)
        except Exception as error:
            if (args.output_dir / 'test.exit.json').exists():
                result['receipt'] = c.load(args.output_dir / 'test.exit.json')
            result['invocation_error'] = str(error)
        receipt = result.get('receipt', {})
        result['gui_invocations'] = int(receipt.get('pid') is not None)
        alive = False
        if receipt.get('pid') is not None:
            try:
                os.killpg(receipt['pid'], 0)
                alive = True
            except ProcessLookupError:
                pass
        result['owned_process_group_exists'] = alive
        result['slot_released'] = not alive and (receipt.get('pid') is not None or receipt.get('state') == 'not_started')
        after = verify()
        c.save(args.output_dir / 'native_binding_after.json', after)
        c.require(after == before, 'Native/source binding changed during the one invocation')
        c.require(c.sha(proof_path) == args.driver_proof_sha256 and c.sha(binary) == driver['binary_sha256'],
                  'Original driver/proof changed during invocation')
        trace = args.output_dir / 'query_trace.jsonl'
        result['trace_present'] = trace.exists() and trace.stat().st_size > 0
        result['selected_call_passed'] = (receipt.get('exit_code') == 0
            and not receipt.get('timed_out') and result['trace_present'] and result['slot_released'])
        result['source_and_native_unchanged'] = True
    except Exception as error:
        result['error'] = str(error)
        if result['gui_invocation_attempts'] == 0:
            result['slot_released'] = True
    finally:
        result['finished_utc'] = c.now()
        c.save(args.output_dir / 'execution.json', result)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get('selected_call_passed') else 1


if __name__ == '__main__':
    sys.exit(main())
