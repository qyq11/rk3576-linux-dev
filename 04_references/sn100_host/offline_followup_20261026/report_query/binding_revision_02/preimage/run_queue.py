"""Execute only after the integration window grants the serial build/GUI slot."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[4]
WORK = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--execute-slot', action='store_true')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--native-build', type=Path, default=ROOT / 'temp/sn100_host/qc_local_20261025/native/wsl_release')
parser.add_argument('--snapshot', type=Path, default=ROOT / 'temp/sn100_host/qc_local_20261025/native/final_before.json')
args = parser.parse_args()
if not args.execute_slot:
    parser.error('Wait for the integration window to allocate the execution slot')
if not args.output_dir.is_absolute() or args.output_dir.exists():
    parser.error('A new absolute output directory is required')
helpers = ROOT / 'temp/sn100_host/qc_local_20261025/integration_drivers/common.py'
spec = importlib.util.spec_from_file_location('report_query_evidence_helpers', helpers)
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
c.NATIVE, c.FREEZE = args.native_build.parent, args.snapshot
args.output_dir.mkdir(parents=True)
deadline = time.monotonic() + 300
result = {'schema': 'sn100-report-query-bounded-probe-v1', 'started_utc': c.now(),
          'diagnostic_only': True, 'production_modified': False, 'runs': [],
          'historical_root_cause_proven': False, 'observer_effect_possible': True}

def logged(name, command, output, env=None, maximum=90):
    remaining = deadline - time.monotonic()
    c.require(remaining > 0, 'Five-minute execution bound reached')
    try:
        return c.run_logged(name, command, output, env=env, timeout=min(maximum, remaining))
    except Exception:
        receipt = output / (name + '.exit.json')
        if receipt.exists():
            return c.load(receipt)
        raise

try:
    prepared = c.load(WORK / 'prepared_sources.json')
    for path, expected in ((ROOT / '06_programs/sn100_host/tests/integrated_report_test.cpp', prepared['original_sha256']),
                           (WORK / 'source/integrated_report_test.cpp', prepared['trace_sha256']),
                           (WORK / 'report_query_trace.h', prepared['trace_header_sha256']),
                           (WORK / 'build_probe.py', prepared['prepared_builder_sha256'])):
        c.require(c.sha(path) == expected, 'Prepared source changed: ' + str(path))
    frozen = c.load(args.snapshot)
    native = c.verify_native(frozen)
    c.save(args.output_dir / 'native_binding_before.json', native)
    build = args.output_dir / 'build'
    command = [sys.executable, '-B', str(WORK / 'build_probe.py'), '--build-dir', str(args.native_build),
               '--snapshot', str(args.snapshot), '--output-dir', str(build), '--jobs', '1', 'integrated_report_test']
    build_receipt = logged('build_trace_driver', command, args.output_dir, maximum=120)
    result['build_receipt'] = build_receipt
    c.require(build_receipt.get('exit_code') == 0 and not build_receipt.get('timed_out'), 'Probe compilation failed')
    old = ROOT / 'temp/sn100_host/catalog_integration/tests/integrated_report_test'
    old_renderer = ROOT / 'temp/sn100_host/catalog_integration/native/wsl_release/sn100_report_renderer'
    old_inputs = {str(old): '1f2b25e41552fecbf8c2a2deede92c21ddddc84bf9743398513ba4af6948c96a',
                  str(old_renderer): 'cc1c0d4562f6a56670a7884a2875ecede77407ae5ee83e7db4587375b8001d3b'}
    for name, expected in old_inputs.items():
        c.require(c.sha(Path(name)) == expected, 'Original frozen executable changed: ' + name)
    c.save(args.output_dir / 'original_binary_inputs.json', old_inputs)
    for name, binary, renderer, qpa, trace in (
        ('original_xcb', old, old_renderer, 'xcb', False),
        ('trace_xcb', build / 'integrated_report_test', args.native_build / 'sn100_report_renderer', 'xcb', True),
        ('trace_offscreen', build / 'integrated_report_test', args.native_build / 'sn100_report_renderer', 'offscreen', True)):
        scenario = args.output_dir / name
        scenario.mkdir()
        env = os.environ.copy()
        selected = {'QT_QPA_PLATFORM': qpa, 'SN100_INTEGRATED_REPORT_EVIDENCE_DIR': str(scenario / 'data'),
                    'SN100_REPORT_RENDERER': str(renderer),
                    'SN100_REPORT_PDF_PYTHON': '/mnt/c/Users/52400/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'}
        if trace:
            selected['SN100_LEGACY_QUERY_TRACE'] = str(scenario / 'query_trace.jsonl')
        env.update(selected)
        c.save(scenario / 'selected_environment.json', selected)
        receipt = logged('test', [str(binary), 'committedHospitalAndReportLifecycle',
            'persistedHospitalAndLegacyEntry', '-o', str(scenario / 'qt.txt') + ',txt',
            '-o', str(scenario / 'qt.xml') + ',xml'], scenario, env=env)
        result['runs'].append({'name': name, 'binary_sha256': c.sha(binary), 'renderer_sha256': c.sha(renderer),
                               'receipt': receipt})
        if trace:
            trace_path = scenario / 'query_trace.jsonl'
            c.require(trace_path.exists() and trace_path.stat().st_size > 0, 'Missing query trace')
        # Preserve failures and continue the planned two contrasts; never retry
        # a failed assertion or replace its outcome with a subsequent pass.
    for name, expected in old_inputs.items():
        c.require(c.sha(Path(name)) == expected, 'Original frozen artifact changed during execution')
    c.verify_native(frozen)
    result['current_source_and_native_unchanged'] = True
    result['all_selected_calls_passed'] = all(row['receipt'].get('exit_code') == 0
        and not row['receipt'].get('timed_out') for row in result['runs'])
except Exception as error:
    result['error'] = str(error)
finally:
    result['finished_utc'] = c.now()
    c.save(args.output_dir / 'execution.json', result)
print(json.dumps(result, ensure_ascii=False, indent=2))
sys.exit(0 if result.get('all_selected_calls_passed') else 1)
