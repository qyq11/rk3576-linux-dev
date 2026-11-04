import json,hashlib,tarfile,pathlib,time
r=pathlib.Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
a=r/'07_backups/sn100_host/source_candidates/20261104_before_temp_cleanup'
m=json.loads((a/'development_files.json').read_text())
expected={x['path']:x for x in m['files']}
seen=set(); failures=[]
with tarfile.open(a/'development_files.tar.gz','r|gz') as tf:
 for ent in tf:
  if not ent.isfile():
   failures.append({'path':ent.name,'reason':'unexpected non-regular entry'});continue
  want=expected.get(ent.name)
  h=hashlib.sha256();size=0
  stream=tf.extractfile(ent)
  while True:
   block=stream.read(1048576)
   if not block:break
   h.update(block);size+=len(block)
  if not want or ent.name in seen or want['sha256']!=h.hexdigest() or want['bytes']!=size:
   failures.append({'path':ent.name,'reason':'manifest mismatch'})
  seen.add(ent.name)
missing=sorted(set(expected)-seen)
result={'checked_members':len(seen),'expected_members':len(expected),'failures':failures,'missing':missing,'complete':not failures and not missing}
(a/'development_verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(result))
raise SystemExit(0 if result['complete'] else 1)
