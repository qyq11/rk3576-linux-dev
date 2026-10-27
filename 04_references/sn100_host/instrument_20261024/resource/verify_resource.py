"""Recompute one completed resource observation; archive no runtime database or secret."""
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import sqlite3

repo = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
work = repo / 'temp/sn100_host/instrument_20261024'
raw = work / 'resource'
runtime = work / 'resource_runtime'
source = work / 'source/06_programs/sn100_host'
output = repo / '04_references/sn100_host/instrument_20261024/resource'


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def copy_exact(origin, target):
    if target.exists():
        if sha(origin) == sha(target):
            return
        raise RuntimeError('Refuse to overwrite different archived evidence: ' + str(target))
    shutil.copy2(origin, target)
    assert sha(origin) == sha(target)


summary = read_json(raw / 'summary.json')
samples = read_json(raw / 'samples.json')
original = read_json(raw / 'verification.json')
log = (raw / 'process.log').read_text(encoding='utf-8')
assert len(samples) == summary['sample_count'] == 295
assert summary['exit_code'] == 0 and not summary['timed_out'] and summary['ordinary_sigterm_requested']
assert all(later['elapsed_seconds'] > earlier['elapsed_seconds'] and later['cpu_ticks'] >= earlier['cpu_ticks']
           for earlier, later in zip(samples, samples[1:]))
clock_ticks = os.sysconf('SC_CLK_TCK')
span = samples[-1]['elapsed_seconds'] - samples[0]['elapsed_seconds']
ticks = samples[-1]['cpu_ticks'] - samples[0]['cpu_ticks']
cpu_percent = 100 * ticks / clock_ticks / span
assert math.isclose(cpu_percent, summary['cpu_percent_one_logical_core'], rel_tol=1e-12)
assert math.isclose(span, summary['observed_span_seconds'], rel_tol=1e-12)
fields = ('VmRSS', 'VmHWM', 'Pss', 'Threads', 'fd_count')
peaks = {field: max(row[field] for row in samples if row.get(field) is not None) for field in fields}
assert all(peaks[field] == summary['maximum_' + field] for field in fields)
pss_unavailable = sum(row.get('Pss') is None for row in samples)
assert pss_unavailable == summary['pss_unavailable_samples'] == 0

tail = [row for row in samples if 10 <= row['elapsed_seconds'] < 30]
tail_span = tail[-1]['elapsed_seconds'] - tail[0]['elapsed_seconds']
tail_ticks = tail[-1]['cpu_ticks'] - tail[0]['cpu_ticks']
tail_analysis = {
    'scope': 'Reanalysis of the same recorded samples with 10 <= elapsed_seconds < 30; not a new observation or a causal before/after comparison.',
    'sample_count': len(tail), 'first_sample_seconds': tail[0]['elapsed_seconds'],
    'last_sample_seconds': tail[-1]['elapsed_seconds'], 'observed_span_seconds': tail_span,
    'cpu_tick_delta': tail_ticks, 'cpu_percent_one_logical_core': 100 * tail_ticks / clock_ticks / tail_span,
    'peaks': {field: max(row[field] for row in tail if row.get(field) is not None) for field in fields},
}

sync = read_json(work / 'final_sync.json')
actual_sources = {p.relative_to(source).as_posix(): sha(p) for p in source.rglob('*')
                  if p.is_file() and p.suffix not in ('.md', '.pyc') and '__pycache__' not in p.parts}
assert len(actual_sources) == 468 and actual_sources == sync['final_sha256']
observer = source / 'tests/observe_linux_process.py'
fixture = source / 'tests/fixtures/status_replay.json'
observer_text = observer.read_text()
assert "int(stat[11]) + int(stat[12])" in observer_text and "os.sysconf('SC_CLK_TCK')" in observer_text
assert sha(observer) == actual_sources['tests/observe_linux_process.py']
assert sha(fixture) == actual_sources['tests/fixtures/status_replay.json']
native_verified = read_json(output.parent / 'native/verification.json')['main_helper_outputs']
binaries = {}
for name in ('sn100_shell', 'sn100_report_renderer'):
    path = work / 'native/wsl_release' / name
    digest = sha(path)
    assert digest == original['binary_sha256'][str(path)] == native_verified[name]['sha256']
    binaries[name] = {'sha256': digest, 'size': path.stat().st_size}
assert not (work / 'resource_bootstrap.tmp').exists()

closed_names = ('HOSPITAL_STORE_CLOSED', 'ACCOUNT_STORE_CLOSED', 'HISTORY_STORE_CLOSED',
                'PROJECT_STORE_CLOSED', 'REAGENT_STORE_CLOSED', 'SAMPLE_STORE_CLOSED',
                'MAINTENANCE_STORE_CLOSED')
closures = {name: log.count(name) for name in closed_names}
assert all(count == 1 for count in closures.values())
assert 'SHELL_READY' in log and 'INSTRUMENT_INITIAL_MONTH_READY' in log
assert 'SHUTDOWN_REQUEST signal= SIGTERM' in log and 'SHUTDOWN_ACCEPTED' in log
assert 'THREADS_STOPPED true' in log and 'STATUS_REPLAY_STOPPED true' in log and 'EXIT code= 0' in log
steps = [(int(number), detail) for number, detail in re.findall(r'^STATUS_REPLAY_STEP (\d+) (.*)$', log, re.M)]
assert [number for number, _ in steps] == list(range(1, 15))
assert 'fresh= false finished= true' in steps[-1][1]

# Read only 16 signature bytes from runtime files. For recognized SQLite files,
# expose table names and COUNT(*) only; no row or account/patient field values.
runtime_files = sorted(p for p in runtime.rglob('*') if p.is_file())
runtime_stats_before = {str(p): (p.stat().st_size, p.stat().st_mtime_ns) for p in runtime_files}
assert len(runtime_files) == original['runtime_file_count']
assert sum(p.stat().st_size for p in runtime_files) == original['runtime_bytes']
database_rows = {}
for path in runtime_files:
    with path.open('rb') as stream:
        signature = stream.read(16)
    if signature != b'SQLite format 3\x00':
        continue
    wal = Path(str(path) + '-wal')
    if wal.exists() and wal.stat().st_size:
        raise RuntimeError('A nonempty WAL requires a separate read-only inspection contract: ' + str(path))
    # Immutable read-only mode prevents journals/SHM or database writes. The
    # observed process has exited and there is no uncheckpointed WAL above.
    with sqlite3.connect(path.as_uri() + '?mode=ro&immutable=1', uri=True) as connection:
        connection.execute('PRAGMA query_only=ON')
        names = [row[0] for row in connection.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT GLOB 'sqlite_*' ORDER BY name")]
        database_rows[path.relative_to(runtime).as_posix()] = {
            name: connection.execute('SELECT count(*) FROM "' + name.replace('"', '""') + '"').fetchone()[0]
            for name in names}
assert {str(p): (p.stat().st_size, p.stat().st_mtime_ns) for p in runtime_files} == runtime_stats_before
for name, tables in original['database_rows'].items():
    assert database_rows[name] == tables
omitted_databases = sorted(set(database_rows) - set(original['database_rows']))

for origin_name, target_name in (('summary.json', 'summary.json'), ('samples.json', 'samples.json'),
                                 ('verification.json', 'verification_original.json'), ('process.log', 'process.txt')):
    copy_exact(raw / origin_name, output / target_name)
copy_exact(work / 'run_resource.py', output / 'run_resource.py')
copy_exact(observer, output / 'observe_linux_process.py')

verification = {
    'schema': 'sn100-instrument-resource-independent-verification-v1',
    'verified_utc': datetime.now(timezone.utc).isoformat(),
    'original_evidence_preserved': True,
    'source_scope': 'Private 468-input source checkpoint exactly equals final_sync.final_sha256. Current formal source has later login work and is not compared.',
    'source_count': len(actual_sources), 'source_sha256_verified': True,
    'observer_source_sha256': sha(observer), 'driver_source_sha256': sha(work / 'run_resource.py'),
    'fixture_sha256': sha(fixture), 'binary_sha256_verified': binaries,
    'load': 'Cold start as anonymous Guest with seven stores and --seed-catalog-demo; 14-event file replay followed by idle on original instrument page. This is not 30 seconds of continuous communication or business workload.',
    'environment': 'WSL Ubuntu24.04 x86_64 / Qt5.15.13 Release / XCB / 1600x900 host over original 1920x1080 shell canvas.',
    'preflight_context': {
        'source': 'Parent agent observations immediately before the run and explicit coordination with the other chat; not reconstructed from a later process list.',
        'windows_compiler_process_count': 0, 'wsl_project_compiler_or_gui_process_seen': False,
        'project_build_compression_large_hash_serialized': True,
        'other_system_services_existed': True,
        'system_fully_background_free_claimed': False,
    },
    'actual_elapsed_seconds': summary['elapsed_seconds'], 'sample_count': len(samples),
    'sample_period_requested_seconds': summary['sample_period_seconds'],
    'first_sample_seconds': samples[0]['elapsed_seconds'], 'last_sample_seconds': samples[-1]['elapsed_seconds'],
    'cpu': {'ticks_per_second_sysconf_SC_CLK_TCK': clock_ticks, 'first_ticks': samples[0]['cpu_ticks'],
            'last_ticks': samples[-1]['cpu_ticks'], 'tick_delta': ticks,
            'observed_span_seconds': span, 'percent_one_logical_core': cpu_percent,
            'formula': '100 * (last_ticks-first_ticks) / SC_CLK_TCK / (last_sample_seconds-first_sample_seconds)',
            'scope': 'Observed direct process only, cumulative user+system ticks; denominator is the sampled span, not the full launch-to-exit duration.'},
    'full_peaks': peaks, 'memory_unit': 'KiB', 'pss_unavailable_samples': pss_unavailable,
    'tail_10_to_30_seconds_same_observation': tail_analysis,
    'exit_code': 0, 'ordinary_sigterm_and_shutdown_accepted': True,
    'seven_store_close_marker_counts': closures, 'thread_stopped': True, 'replay_stopped': True,
    'replay_event_count': len(steps), 'replay_ended_with_fresh_false': True,
    'runtime_file_count': len(runtime_files), 'runtime_bytes': original['runtime_bytes'],
    'database_rows': database_rows,
    'database_row_correction': {
        'original_selection': "runtime.rglob('*.db')",
        'corrected_selection': 'Read first 16 signature bytes of each closed-runtime file, then open only SQLite signatures read-only/immutable and query schema table names and COUNT(*).',
        'original_database_count': len(original['database_rows']), 'corrected_database_count': len(database_rows),
        'previously_omitted_databases': omitted_databases,
        'original_listed_counts_preserved': True, 'row_field_values_read': False,
        'runtime_file_sizes_and_mtimes_unchanged': True,
    },
    'secret_file_deleted': True, 'runtime_databases_tags_or_secret_archived': False,
    'limitations': ['No accepted resource budget', 'No RK measurement or same-board real-time control qualification',
                    'No real instrument action', 'No long-term stability claim',
                    'Tail is the same observation, with no causal replay-before/after conclusion',
                    'Other system services existed; project serialization is not proof of a fully idle operating system'],
    'audit_started_no_gui_build_or_device': True,
}
write_json(output / 'verification.json', verification)
records = [{'path': p.relative_to(output).as_posix(), 'size': p.stat().st_size, 'sha256': sha(p)}
           for p in sorted(output.iterdir()) if p.is_file() and p.name != 'manifest.json']
write_json(output / 'manifest.json', {'schema': 'sn100-evidence-manifest-v1', 'files': records})
assert all(sha(output / record['path']) == record['sha256'] for record in records)
print(json.dumps({'sample_count': len(samples), 'elapsed_seconds': summary['elapsed_seconds'],
                  'SC_CLK_TCK': clock_ticks, 'cpu_ticks': ticks, 'cpu_percent_one_core': cpu_percent,
                  'peaks': peaks, 'tail': tail_analysis,
                  'database_count': len(database_rows), 'omitted_databases': omitted_databases,
                  'manifest_files': len(records)}, ensure_ascii=False))
