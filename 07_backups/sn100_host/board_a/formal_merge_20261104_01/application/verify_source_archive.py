import pathlib,json,hashlib,tarfile
r=pathlib.Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
a=r/'07_backups/sn100_host/board_a/formal_merge_20261104_01/application'
m=json.loads((a/'source_snapshot_sorted.json').read_text())
expected={'06_programs/sn100_host/'+p:h for p,h in m.items()}
seen=set();fail=[]
with tarfile.open(a/'source_files.tar.gz','r:gz') as tf:
 for e in tf:
  if not e.isfile(): fail.append(e.name);continue
  b=tf.extractfile(e).read()
  if e.name not in expected or e.name in seen or hashlib.sha256(b).hexdigest()!=expected[e.name]:fail.append(e.name)
  seen.add(e.name)
result={'members':len(seen),'expected':len(expected),'mismatches':fail,'missing':sorted(set(expected)-seen)}
(a/'source_verification.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
raise SystemExit(0 if not fail and len(seen)==len(expected) else 1)
