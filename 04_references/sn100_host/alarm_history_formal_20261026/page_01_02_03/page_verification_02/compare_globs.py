import hashlib
import json
from pathlib import Path

here = Path(__file__).resolve().parent
root = here.parents[3]
document = json.loads((here / 'source_manifest.json').read_text())
results = []
for item in document['glob_inventories']:
    pattern = root / item['pattern']
    found = list(pattern.parent.glob(pattern.name))
    rows = []
    for path in found:
        if not path.is_file():
            continue
        normalized = path.resolve(strict=True).relative_to(root).as_posix()
        expected = document['files'].get(normalized)
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        rows.append({'WSL_raw_relative': path.relative_to(root).as_posix(),
                     'WSL_normalized_relative': normalized,
                     'bytes': path.stat().st_size, 'sha256': digest,
                     'exact_case_and_expected_member': normalized in item['files'],
                     'frozen_size_and_bytes_match': expected is not None and
                         expected['bytes'] == path.stat().st_size and expected['sha256'] == digest})
    normalized_found = sorted(row['WSL_normalized_relative'] for row in rows)
    expected_sorted = sorted(item['files'])
    results.append({'pattern': item['pattern'], 'Windows_recorded_order': item['files'],
                    'expected_sorted': expected_sorted, 'WSL_raw_order': [row['WSL_raw_relative'] for row in rows],
                    'WSL_normalized_sorted': normalized_found, 'members': rows,
                    'full_expected_set_unchanged': expected_sorted == normalized_found,
                    'only_raw_path_representation_differs': expected_sorted != sorted(row['WSL_raw_relative'] for row in rows) and
                        expected_sorted == normalized_found,
                    'every_case_size_and_byte_matches': all(row['exact_case_and_expected_member'] and
                        row['frozen_size_and_bytes_match'] for row in rows)})
passed = len(results) == 4 and all(row['full_expected_set_unchanged'] and
    row['only_raw_path_representation_differs'] and row['every_case_size_and_byte_matches'] for row in results)
value = {'schema': 'sn100-page-glob-normalization-difference-v1',
         'scope': 'Read-only path/case/sort/size/SHA comparison; no build/GUI/SQL',
         'passed': passed, 'globs': results}
(here / 'glob_comparison.json').write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
print(json.dumps({'passed': passed, 'glob_count': len(results),
                  'member_count': sum(len(row['members']) for row in results)}))
raise SystemExit(0 if passed else 1)
