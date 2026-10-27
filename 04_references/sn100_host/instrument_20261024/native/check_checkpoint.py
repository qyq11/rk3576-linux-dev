import importlib.util
import json
from pathlib import Path

work=Path(__file__).resolve().parent
root=work.parents[2]
program=work/'source/06_programs/sn100_host'
spec=importlib.util.spec_from_file_location('instrument_checkpoint',program/'shell/package_arm64.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
current=module.snapshot(program)
expected=json.loads((root/'temp/sn100_host/o09_loopback/native/interim_after.json').read_text())
result={'source_input_count':len(current),'expected_input_count':len(expected),
        'matches_validated_o09_checkpoint':current==expected,
        'differences':[name for name in sorted(set(current)|set(expected)) if current.get(name)!=expected.get(name)]}
(work/'checkpoint_inputs.json').write_text(json.dumps(current,indent=2)+'\n')
(work/'checkpoint_verification.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
raise SystemExit(0 if current==expected else 1)
