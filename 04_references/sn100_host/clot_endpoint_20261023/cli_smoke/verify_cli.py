"""Host-only basic contract checks; real SN100/assay testing is deferred."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys

binary, fixture, destination = map(Path, sys.argv[1:])
destination.mkdir()
source = json.loads(fixture.read_text(encoding='utf-8'))
checks = []

def run(name, data, code):
    input_path = destination / (name + '.input.json')
    input_path.write_text(json.dumps(data), encoding='utf-8')
    output_path = destination / (name + '.result.json')
    result = subprocess.run([str(binary), '--input', str(input_path), '--output', str(output_path)],
                            text=True, capture_output=True, timeout=10)
    (destination / (name + '.stdout.log')).write_text(result.stdout, encoding='utf-8')
    (destination / (name + '.stderr.log')).write_text(result.stderr, encoding='utf-8')
    assert result.returncode == code, (name, result.returncode, result.stderr)
    if code == 2:
        assert not output_path.exists(), name
    output = json.loads(output_path.read_text(encoding='utf-8')) if output_path.exists() else None
    checks.append({'name': name, 'exit_code': result.returncode, 'expected_exit_code': code})
    return input_path, output_path, output

_, first_path, first = run('asymmetric_hand_golden', source, 0)
assert abs(first['percentage']['time_seconds'] - 11.2) < 1e-12
assert first['percentage']['threshold_value'] == 60
assert first['maximum_derivative']['time_seconds'] == 11
assert abs(first['maximum_derivative']['peak_slope_optical_units_per_second'] - 20) < 1e-12
assert not first['clinical_validated'] and not first['hardware_verified']

falling = copy.deepcopy(source)
falling['direction'] = 'falling'
falling['points'] = [[t, 120 - value] for t, value in source['points']]
_, _, fall = run('falling_direction', falling, 0)
assert fall['percentage']['time_seconds'] == first['percentage']['time_seconds']
assert fall['maximum_derivative']['time_seconds'] == first['maximum_derivative']['time_seconds']

shifted = copy.deepcopy(source)
shifted['points'] = [[t + 7, value] for t, value in source['points']]
shifted['parameters']['search_start_seconds'] += 7
shifted['parameters']['search_end_seconds'] += 7
_, _, shift = run('nonzero_acquisition_start', shifted, 0)
assert abs(shift['percentage']['time_seconds'] - 18.2) < 1e-12
assert shift['maximum_derivative']['time_seconds'] == 18

for name, mutate, code in [
    ('incomplete', lambda d: d.update(complete_curve=False), 2),
    ('unknown_parameter', lambda d: d['parameters'].update(derivatve_window_points=5), 2),
    ('duplicate_time', lambda d: d['points'].__setitem__(1, d['points'][0]), 4),
    ('negative_time', lambda d: d['points'][0].__setitem__(0, -1), 4),
    ('flat_no_response', lambda d: d.update(points=[[t, 10] for t, _ in d['points']]), 4),
    ('zero_threshold', lambda d: d['parameters'].update(threshold_percent=0), 4),
    ('wrong_direction', lambda d: d.update(direction='falling'), 4),
]:
    data = copy.deepcopy(source)
    mutate(data)
    run(name, data, code)

before = hashlib.sha256(first_path.read_bytes()).hexdigest()
result = subprocess.run([str(binary), '--input', str(fixture), '--output', str(first_path)], capture_output=True, timeout=10)
assert result.returncode == 2 and hashlib.sha256(first_path.read_bytes()).hexdigest() == before
checks.append({'name': 'existing_output_unchanged', 'exit_code': 2, 'sha256': before})

root = fixture.resolve().parents[5]
forbidden = root / '06_programs/sn100_host_reference/should_not_create_clot_result.json'
assert not forbidden.exists()
result = subprocess.run([str(binary), '--input', str(fixture), '--output', str(forbidden)], capture_output=True, timeout=10)
assert result.returncode == 2 and not forbidden.exists()
checks.append({'name': 'reference_output_refused', 'exit_code': 2})

summary = {'scope': 'Synthetic input and CLI smoke only; no clinical, instrument or AL4A validation',
           'all_passed': True, 'count': len(checks), 'checks': checks}
(destination / 'verification.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print(json.dumps(summary, indent=2))
