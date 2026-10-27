#!/usr/bin/env python3
"""Copy explicit repair evidence only; no runtime, executable or object files."""
import difflib
import json
from pathlib import Path
import re
import shutil
import sys

REPAIR = Path(__file__).resolve().parent
WORK = REPAIR.parent
sys.path.insert(0, str(WORK / 'integration_drivers'))
from common import ROOT, PROGRAM, NATIVE, FREEZE, load, save, sha, require, now, verify_native


def main():
    formal = ROOT / '04_references/sn100_host/qc_local_20261025'
    output = formal / 'report_shell_repair_20261025'
    handoff_path = formal / 'report_shell_repair_handoff.json'
    require(not output.exists() and not handoff_path.exists(), 'New formal destination required')
    native = verify_native(load(FREEZE))
    final = REPAIR / 'attempt_03'
    proof_path = final / 'driver_build_01/binding_proof.json'
    proof = load(proof_path)
    driver = proof['drivers']['report_remaining_shell_test']
    require(proof['native_objects_excluding_main'] == 167 and proof['snapshot_sha256'] == sha(FREEZE), 'Exact frozen binding required')
    require(sha(final / 'driver_build_01/report_remaining_shell_test') == driver['binary_sha256'], 'Final binary changed')
    require(sha(PROGRAM / 'tests/report_remaining_shell_test.cpp') == driver['source_sha256'], 'Final source changed')
    runs = []
    for qpa in ('offscreen', 'xcb'):
        stage = final / ('run_01_' + qpa)
        verification = load(stage / 'verification.json')
        receipt = load(stage / 'test.exit.json')
        totals = re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped[^\n]*', (stage / 'qt.txt').read_text())
        require(verification['passed'] is True and verification['actual_exit'] == 0 and verification['totals'] == [5, 0, 0], 'Actual complete QPA pass required')
        require(receipt['exit_code'] == 0 and receipt['state'] == 'finished' and not receipt['timed_out'], 'Actual successful original receipt required')
        require(totals and totals[-1] == ('5', '0', '0'), 'Raw Qt totals mismatch')
        require(verification['source_sha256'] == driver['source_sha256'] and verification['driver_sha256'] == driver['binary_sha256']
                and verification['binding_proof_sha256'] == sha(proof_path), 'Run provenance mismatch')
        require(verification['main_artifacts'] == native['artifacts'], 'Run product SHA mismatch')
        runs.append({'qpa': qpa, 'actual_exit': receipt['exit_code'], 'totals': [5, 0, 0], 'elapsed_seconds': receipt['seconds'],
                     'raw': (output / 'attempt_03' / stage.name).relative_to(ROOT).as_posix()})
    output.mkdir()
    copied = {}
    allowed = {'.json', '.txt', '.xml', '.log', '.exit', '.png', '.html', '.pdf', '.cpp', '.pro', '.py'}

    def copy_file(source, relative):
        require(source.is_file() and source.suffix.lower() in allowed, 'Only explicit safe file types allowed: ' + str(source))
        require(not ({'runtime', '.obj', '.moc', '.ui'} & set(source.relative_to(ROOT).parts)), 'Forbidden runtime/build data')
        target = output / relative
        require(target.resolve().is_relative_to(output.resolve()), 'Destination escape')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        require(sha(source) == sha(target), 'Evidence copy SHA mismatch')
        copied[target.relative_to(output).as_posix()] = {'source': source.relative_to(ROOT).as_posix(),
                                                       'sha256': sha(target), 'bytes': target.stat().st_size}

    def copy_run(stage, relative):
        for name in ('test.command.json', 'test.exit.json', 'test.exit', 'test.process.json', 'test.log', 'qt.txt', 'qt.xml'):
            copy_file(stage / name, relative / name)
        if (stage / 'verification.json').is_file():
            copy_file(stage / 'verification.json', relative / 'verification.json')
        if (stage / 'evidence').is_dir():
            for source in sorted((stage / 'evidence').rglob('*')):
                if source.is_file():
                    require(source.suffix.lower() in {'.png', '.html', '.pdf'}, 'Unexpected evidence file')
                    copy_file(source, relative / 'evidence' / source.relative_to(stage / 'evidence'))

    old = WORK / 'suite_gate_04/report_remaining_shell_offscreen'
    copy_run(old, Path('root_suite_gate_04_failed'))
    timeline = [{'attempt': 'root suite_gate_04', 'exit_code': load(old / 'test.exit.json')['exit_code'],
                 'raw_totals': re.findall(r'Totals:[^\n]*', (old / 'qt.txt').read_text())[-1]}]
    for attempt in sorted(REPAIR.glob('attempt_[0-9][0-9]')):
        relative = Path(attempt.name)
        copy_file(attempt / 'source.json', relative / 'source.json')
        for source in sorted((attempt / 'source').iterdir()):
            copy_file(source, relative / 'source' / source.name)
        for name in ('build.command.json', 'build.exit.json', 'build.exit', 'build.process.json', 'build.log'):
            copy_file(attempt / name, relative / name)
        copy_file(attempt / 'driver_build_01/binding_proof.json', relative / 'build/binding_proof.json')
        for source in sorted((attempt / 'driver_build_01/logs').iterdir()):
            copy_file(source, relative / 'build/logs' / source.name)
        for stage in sorted(attempt.glob('run_[0-9][0-9]_*')):
            copy_run(stage, relative / stage.name)
            timeline.append({'attempt': attempt.name, 'run': stage.name, 'exit_code': load(stage / 'test.exit.json')['exit_code'],
                             'raw_totals': re.findall(r'Totals:[^\n]*', (stage / 'qt.txt').read_text())[-1]})
    preimage = ROOT / '07_backups/sn100_host/offline_four_windows_20261025/review_test_preimage/report_shell'
    for source in sorted(preimage.iterdir()):
        copy_file(source, Path('preimage') / source.name)
    copy_file(Path(__file__), Path('tools/archive_repair.py'))
    copy_file(REPAIR / 'repair_driver.py', Path('tools/repair_driver.py'))
    patch = ''.join(difflib.unified_diff((preimage / 'report_remaining_shell_test.cpp').read_text().splitlines(True),
        (PROGRAM / 'tests/report_remaining_shell_test.cpp').read_text().splitlines(True), fromfile='before.cpp', tofile='after.cpp'))
    (output / 'harness_change.diff').write_text(patch)
    save(output / 'copied_files.json', copied)
    for name, row in copied.items():
        require(sha(output / name) == row['sha256'], 'Final copy readback mismatch')
    sums = {path.relative_to(output).as_posix(): sha(path) for path in sorted(output.rglob('*')) if path.is_file()}
    (output / 'SHA256SUMS').write_text(''.join(digest + '  ' + name + '\n' for name, digest in sums.items()))
    for line in (output / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1)
        require(sha(output / name) == digest, 'Manifest readback mismatch')
    handoff = {'schema': 'sn100-report-shell-test-repair-handoff-v1', 'finished_utc': now(),
        'status': 'Completed; this repair has stopped all source writes, builds and GUI runs',
        'scope': 'Test harness only; no production changes and no Windows/RK/instrument validation',
        'changed_source': {'path': '06_programs/sn100_host/tests/report_remaining_shell_test.cpp', 'sha256': driver['source_sha256']},
        'unchanged_pro': {'path': '06_programs/sn100_host/tests/report_remaining_shell_test.pro', 'sha256': sha(PROGRAM / 'tests/report_remaining_shell_test.pro')},
        'preimage': preimage.relative_to(ROOT).as_posix(), 'failure_and_retest': timeline,
        'repairs': ['Wait for the original toolbar layout and use real mouse clicks on its original action button; click the original extension when overflowed and then its expanded original button or actual QMenu actionGeometry. No QAction::trigger or production geometry changes.',
            'After Cancel and deferred deletion restore the actual visible preview activation. Before typing activate the real QFileDialog and editor, use Ctrl+A/type and its real Save button, and compare the actual fileSelected path before closing the preview.',
            'Keep all session/Guest/six-role, display draft Cancel/Discard, SQL hide/reopen, expected-document exact one-row APTT0.00 s HTML, foreground/background export and busy navigation/login/logout/close assertions and original timeout values.'],
        'observed_toolbar_paths': {'offscreen': ['expanded', 'direct'], 'xcb': ['direct', 'direct']},
        'binding': {'source_inputs': len(load(FREEZE)), 'source_snapshot_sha256': sha(FREEZE),
            'native_objects_excluding_main': proof['native_objects_excluding_main'], 'native_makefile_sha256': proof['native_makefile_sha256'],
            'main_artifacts': native['artifacts'], 'driver': (final / 'driver_build_01/report_remaining_shell_test').relative_to(ROOT).as_posix(),
            'driver_sha256': driver['binary_sha256'], 'binding_proof': (output / 'attempt_03/build/binding_proof.json').relative_to(ROOT).as_posix(),
            'binding_proof_sha256': sha(proof_path)},
        'runs': runs, 'raw_evidence': output.relative_to(ROOT).as_posix(), 'sha256_manifest': sha(output / 'SHA256SUMS'),
        'privacy': 'Explicit source/metadata/compiler and test receipts/raw logs/XML/anonymous PNG and HTML only. Runtime SQLite/INI/secret files, native objects and binaries remain in temp and were excluded.',
        'parent_followup': 'Bind this exact repaired test source/binary/proof into cumulative_build, then run the final cumulative suite. Old cumulative driver is not a pass for the repaired source.',
        'pending': ['Parent final cumulative suite/platform/archive/resource gates', 'Original Windows theme/function comparison, RK and real-instrument validation']}
    save(handoff_path, handoff)
    require(load(handoff_path) == handoff, 'Handoff readback mismatch')
    print(json.dumps({'handoff': handoff_path.relative_to(ROOT).as_posix(), 'handoff_sha256': sha(handoff_path),
                      'archived_files': len(sums), 'source_sha256': driver['source_sha256'], 'passed_qpas': len(runs)}), flush=True)


if __name__ == '__main__':
    main()
