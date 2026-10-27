import hashlib
import json
from pathlib import Path
import re

work=Path(__file__).resolve().parent
result=json.loads((work/'native_final_result.json').read_text())
log=(work/'logs/native_final_build.log').read_text()
compiled=re.findall(r'^g\+\+ -c .*? -o (\S+\.o)(?:\s|$)',log,re.M)
objects=list((work/'native').rglob('*.o'))
audit={'exit_code':result['exit_code'],'source_count':result['source_count'],
       'inputs_unchanged':result['inputs_unchanged'],
       'flags_identical':all(v['identical'] for v in result['flags_gate'].values()),
       'final_object_count':len(objects),'actual_compile_invocations':len(compiled),
       'compiled_objects':compiled,'objects_not_recompiled':len(objects)-len(compiled),
       'same_sha_as_initial_count':result['reused_object_count'],
       'sha_changed_or_added_count':len(result['changed_or_added_objects']),
       'note':'The raw result reused_object_count counts equal SHA values, including rebuilt identical objects; it is not the count of make reuse. Actual compiler invocations are recorded separately.',
       'outputs':result['outputs'],
       'objects_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in objects}}
(work/'native_final_audit.json').write_text(json.dumps(audit,indent=2)+'\n')
print(json.dumps({k:v for k,v in audit.items() if k not in ('compiled_objects','objects_sha256')}))
