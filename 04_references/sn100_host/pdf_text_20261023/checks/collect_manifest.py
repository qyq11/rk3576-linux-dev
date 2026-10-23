from pathlib import Path
import hashlib,json,re,struct,subprocess
root=Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'); work=root/'temp/sn100_host/history/pdf_text_20261023'; out=work/'dependency_logs'; out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
paths=[('native_main',root/'temp/sn100_host/history/wsl_release/sn100_history',62),('native_renderer',root/'temp/sn100_host/history/wsl_release/sn100_report_renderer',62),('arm64_main',work/'arm64/sn100_history',183),('arm64_renderer',work/'arm64/sn100_report_renderer',183),('windows_main',work/'windows/history/release/sn100_history.exe',0x14c),('windows_renderer',work/'windows/renderer/release/sn100_report_renderer.exe',0x14c)]
artifacts=[]
for role,p,expected in paths:
    b=p.read_bytes(); arch=struct.unpack_from('<H',b,18)[0] if b.startswith(b'\x7fELF') else struct.unpack_from('<H',b,struct.unpack_from('<I',b,0x3c)[0]+4)[0]; assert arch==expected
    artifacts.append({'role':role,'path':str(p.relative_to(root)),'machine':{62:'x86_64',183:'AArch64',0x14c:'i386'}[arch],'sha256':sha(p),'size_bytes':len(b)})
    if expected==62:
        r=subprocess.run(['ldd',str(p)],check=True,capture_output=True,text=True); assert 'not found' not in r.stdout; (out/(role+'_ldd.txt')).write_text(r.stdout)
    elif expected==183:
        r=subprocess.run(['aarch64-linux-gnu-readelf','-d',str(p)],check=True,capture_output=True,text=True); (out/(role+'_dynamic.txt')).write_text(r.stdout)
sysroot=root/'temp/sn100_host/arm64_sysroot'; dirs=[sysroot/'usr/lib/aarch64-linux-gnu',sysroot/'lib/aarch64-linux-gnu',Path('/usr/aarch64-linux-gnu/lib')]; queue=[p for _,p,arch in paths if arch==183]; resolved={}
while queue:
    p=queue.pop(0); data=subprocess.run(['aarch64-linux-gnu-readelf','-d',str(p)],check=True,capture_output=True,text=True).stdout
    for name in re.findall(r'\(NEEDED\).*?\[(.*?)\]',data):
        if name in resolved:continue
        matches=[d/name for d in dirs]+[q for d in dirs for q in sorted(d.glob(name+'.*'))]; q=next((x for x in matches if x.is_file()),None); assert q is not None,(name,p); b=q.read_bytes(); assert b.startswith(b'\x7fELF') and struct.unpack_from('<H',b,18)[0]==183
        resolved[name]={'path':str(q),'sha256':sha(q)}; queue.append(q)
sources={}
for folder in ['06_programs/sn100_host/history','06_programs/sn100_host/thirdparty/qtrpt']:
    for p in sorted((root/folder).rglob('*')):
        if p.is_file():sources[str(p.relative_to(root))]=sha(p)
for pattern in ['history_pdf*','verify_history_pdf_text.py']:
    for p in (root/'06_programs/sn100_host/tests').glob(pattern):
        if p.is_file():sources[str(p.relative_to(root))]=sha(p)
unchanged=json.loads((work/'unchanged_before.json').read_text(encoding='utf-8-sig'))
for row in unchanged:row['after_sha256']=sha(root/row['path']); row['unchanged']=row['sha256']==row['after_sha256']; assert row['unchanged']
vendor=json.loads((root/'06_programs/sn100_host/thirdparty/qtrpt/origins.json').read_text(encoding='utf-8-sig')); patches={p['path']:p['current_sha256'] for p in vendor['local_patches']}
for row in vendor['files']:assert sha(root/'06_programs/sn100_host/thirdparty/qtrpt'/row['path'])==patches.get(row['path'],row['copied_sha256'])
manifest={'scope':'Release native/Windows32/ARM64; no RK/instrument/physical printer execution or system installation','artifacts':artifacts,'sources_sha256':sources,'unchanged_core_forms_template':unchanged,'thirdparty_unmodified_files':len(vendor['files'])-len(patches),'thirdparty_patched_files':patches,'arm64_needed_closure':{'missing':[],'libraries':resolved},'new_production_dependencies':[]}
(work/'build_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n'); print(json.dumps({'artifacts':artifacts,'arm_libraries':len(resolved),'core_forms_template_all_unchanged':True}))
