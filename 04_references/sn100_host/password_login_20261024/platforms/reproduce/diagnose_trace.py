from pathlib import Path
import json

arm = Path(__file__).resolve().parent / 'arm64'
log = (arm / 'prepare.log').read_text()
objects = sorted((arm / 'arm64_build').glob('obj_*.o')) + sorted((arm / 'arm64_build').glob('helper_*.o'))
missing = [str(path) for path in objects if '-o ' + str(path) not in log]
split = [path.name for path in objects if not any('-c ' in line and str(path) in line for line in log.splitlines())]
report = {'objects': len(objects), 'missing_output_tokens': missing, 'same_line_parser_failures': split,
          'original_log_sha256': __import__('hashlib').sha256(log.encode()).hexdigest()}
(arm / 'trace_diagnostic.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
