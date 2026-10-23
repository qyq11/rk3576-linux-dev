import json
import math
from pathlib import Path

root = Path(__file__).resolve().parents[3]
fixture = root / '06_programs/sn100_host/coagulation/tests/fixtures/synthetic_curve.json'
case = json.loads(fixture.read_text(encoding='utf-8'))
case['description'] = 'Synthetic 100000-point logistic curve for one WSL resource-budget observation, not clinical accuracy validation'
case['parameters'].update(baseline_points=100, plateau_points=100, derivative_window_points=101,
                          search_start_seconds=10, search_end_seconds=990)
case['points'] = [[index / 100, 10 + 100 / (1 + math.exp(-((index / 100) - 500) / 20))]
                  for index in range(100000)]
path = root / 'temp/sn100_host/coagulation/budget_100000.input.json'
with path.open('x', encoding='utf-8') as file:
    json.dump(case, file, separators=(',', ':'))
print(path.stat().st_size)
