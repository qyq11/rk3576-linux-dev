import pathlib,os,hashlib,json,shutil,signal,time,subprocess
r=pathlib.Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
task=r/'temp/sn100_host/formal_merge_20261104_01'
headers=task/'sysroot/usr/include/aarch64-linux-gnu/qt5'
cache=pathlib.Path('/home/qyq/.cache/sn100_host/formal_merge_20261104_01/qt5')
if cache.exists(): raise SystemExit('fresh cache path already exists')
cache.parent.mkdir(parents=True,exist_ok=True)
shutil.copytree(headers,cache,symlinks=True)
checks=0
for f in headers.rglob('*'):
 if f.is_file():
  other=cache/f.relative_to(headers)
  if hashlib.sha256(f.read_bytes()).digest()!=hashlib.sha256(other.read_bytes()).digest(): raise SystemExit('header copy mismatch')
  checks+=1
# Only this task's running compiler tree is stopped; unrelated builds and board processes are out of scope.
matched=[]
for p in pathlib.Path('/proc').iterdir():
 if not p.name.isdigit() or int(p.name)==os.getpid(): continue
 try:
  args=(p/'cmdline').read_bytes().split(b'\0')
 except (OSError,ProcessLookupError):continue
 if any(bytes(task) in a for a in args):
  matched.append({'pid':int(p.name),'executable':pathlib.Path(os.fsdecode(args[0])).name})
for row in matched:
 try:os.kill(row['pid'],signal.SIGTERM)
 except ProcessLookupError:pass
time.sleep(1)
remaining=[]
for row in matched:
 try:
  status=pathlib.Path('/proc')/str(row['pid'])/'status'
  if status.exists() and 'State:\tZ' not in status.read_text(): remaining.append(row)
 except OSError:pass
if remaining: raise SystemExit('compiler did not stop: '+str(remaining))
original=headers.with_name('qt5.restored_from_archive')
headers.rename(original)
headers.symlink_to(cache,target_is_directory=True)
result={'copied_headers_verified':checks,'cache':str(cache),'same_bytes':True,'stopped_build_processes':matched,'reason':'Use the same archived ARM64 headers on the Linux filesystem to avoid repeated Windows filesystem overhead. No source/flags changed; full compilation starts again without reusing objects.'}
(task/'native_header_cache.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
