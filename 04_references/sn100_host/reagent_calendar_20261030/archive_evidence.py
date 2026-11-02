"""收存本轮构建及RK匿名证据，不复制可变数据库或认证内容。"""
from pathlib import Path
import hashlib,json,shutil,re
w=Path(__file__).resolve().parent;r=w.parents[2];d=r/'04_references/sn100_host/reagent_calendar_20261030';d.mkdir(parents=True,exist_ok=True)
for source in w.iterdir():
    if source.is_file() and source.suffix in ('.py','.sh','.log','.json','.txt') or source.is_file() and source.name.endswith('exit_code'):
        shutil.copy2(source,d/source.name)
for folder in w.iterdir():
    if not folder.is_dir():continue
    if (folder/'remote_path').is_file():
        for source in folder.rglob('*'):
            if source.is_file() and source.suffix.lower() not in ('.db','.sqlite','.gz','.tar'):
                out=d/source.relative_to(w);out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,out)
    elif folder.name.endswith('_rk_test_build'):
        for name in ('test_source.cpp','test_source.SHA256SUMS','object_identity.json','binaries.SHA256SUMS','inputs.tsv'):
            source=folder/name
            if source.is_file():
                out=d/folder.name/name;out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,out)
for name in ('dependency_report.json','dependency_report.txt'):
    source=w/'dependencies'/name
    if source.is_file():
        (d/'dependencies').mkdir(exist_ok=True);shutil.copy2(source,d/'dependencies'/name)
summary={}
for folder in d.iterdir():
    file=folder/'resources.tsv'
    if not file.is_file():continue
    rows=[line.split('\t') for line in file.read_text().splitlines()[1:]]
    rows=[a for a in rows if len(a)>=8 and all(a[i].replace('.','',1).isdigit() for i in range(7))]
    if not rows:continue
    runtime=(folder/'runtime.txt').read_text();ticks=[int(a) for a in runtime.splitlines() if re.fullmatch(r'\d+',a)]
    hz=ticks[0] if ticks else None;first,last=rows[0],rows[-1];seconds=float(last[0])-float(first[0])
    temperatures=[int(t) for a in rows for t in a[7].split(';') if t.isdigit()]
    summary[folder.name]={'samples':len(rows),'seconds':round(seconds,3),'clock_ticks_per_second':hz,
        'driver_cpu_percent_one_core':(int(last[1])+int(last[2])-int(first[1])-int(first[2]))/hz/seconds*100 if hz and seconds else None,
        'peak_rss_kib':max(int(a[3]) for a in rows),'peak_pss_kib':max(int(a[4]) for a in rows),
        'peak_threads':max(int(a[5]) for a in rows),'peak_fds':max(int(a[6]) for a in rows),
        'peak_temperature_mC':max(temperatures) if temperatures else None,
        'scope':'RK驱动初始化、操作及关闭短测；CPU不含独立子程序，非静置、长稳、物理输入或已确认预算。'}
(d/'resource_summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
files=sorted(p for p in d.rglob('*') if p.is_file() and p.name not in ('README.md','SHA256SUMS'))
(d/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(d))+'\n' for p in files))
print('ARCHIVED_FILES='+str(len(files)))
