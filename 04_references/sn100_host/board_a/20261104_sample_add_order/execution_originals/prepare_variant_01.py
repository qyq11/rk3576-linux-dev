"""独立板A验证包：绑定同批15正式对象及仅改板身份的专项，不运行ELF。"""
from pathlib import Path,PurePosixPath
import hashlib,io,json,struct,subprocess,tarfile,shutil
ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
RESUME=ROOT/'temp/sn100_host/resume_dispatch_20261103'
WORK=ROOT/'temp/sn100_host/workflow_next_20261103/integration_15_01'
EXEC=RESUME/'sample_checker15_board_a_exec_01_01'
OUT=ROOT/'07_backups/sn100_host/board_a/sample_order_20261104_01/application'
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def rec(p):return {'path':p.relative_to(ROOT).as_posix(),'sha256':sha(p),'bytes':p.stat().st_size}
def put(p,v):
    with p.open('x',encoding='utf-8') as f:json.dump(v,f,ensure_ascii=False,indent=2);f.write('\n')
def wsl(p):return '/mnt/c/'+p.as_posix()[3:]
def main():
    result=json.loads((EXEC/'wrapper_result.json').read_bytes())
    assert result['actual_exit']==0 and result['compiled'] is True
    assert (RESUME/'board_a_checker_build_01.exit').read_text().strip()=='0'
    assert (EXEC/'wrapper.exit').read_text().strip()=='0'
    binary=EXEC/'build_01/sample_add_review'
    private=EXEC/'build_01/lib/libQt5Test.so.5'
    assert sha(binary)==result['checker_binary']['sha256'] and sha(private)==result['private_qt_test']['sha256']
    for line in (EXEC/'WRAPPER_SHA256SUMS').read_text().splitlines():
        digest,name=line.split('  ',1)
        path=EXEC/name
        assert not path.is_symlink() and sha(path)==digest
    complete=json.loads((WORK/'build_complete.json').read_bytes())
    parent=WORK/'packages'/complete['package']
    assert sha(parent)==complete['package_sha256']=='67a05c08c93ae86515615a5434a6b9200718aa71519dc6800aae5a6eafe9b935'
    assert complete['release_id']==result['formal_release']
    OUT.mkdir()
    dependency=HERE/'checker_a_dependencies_01'
    assert not dependency.exists()
    argv=['wsl.exe','-d','Ubuntu-24.04','-u','qyq','--','env','-u','PYTHONOPTIMIZE','python3','-B',
        wsl(ROOT/'06_programs/sn100_host/shell/check_arm64.py'),wsl(binary),'--additional-binary',wsl(WORK/'build/sn100_report_renderer'),
        '--sysroot',wsl(ROOT/'temp/sn100_host/rk_prepare/sysroot'),'--output-dir',wsl(dependency)]
    put(HERE/'checker_a_dependencies_01.command.json',{'argv':argv,'cwd':str(ROOT),'scope':'static_elf_metadata_only'})
    actual=subprocess.run(argv,cwd=ROOT,capture_output=True)
    for suffix,raw in (('.stdout.log',actual.stdout),('.stderr.log',actual.stderr),('.exit',(str(actual.returncode)+'\n').encode())):
        with (HERE/('checker_a_dependencies_01'+suffix)).open('xb') as f:f.write(raw)
    assert actual.returncode==0
    report=dependency/'dependency_report.json'
    dep=json.loads(report.read_bytes())
    assert dep['errors']==[] and dep['board_runtime_verified'] is False and len(dep['files'])==57
    assert dep['binary']==wsl(binary)
    sources=json.loads((WORK/'source_snapshot.json').read_bytes())
    assert len(sources)==697
    for name,digest in sources.items():assert sha(ROOT/'06_programs/sn100_host'/name)==digest
    basis={'schema':'sn100-board-a-sample-variant-v1','board':'board_a','board_serial':'3bf8222fe1417f6a',
        'parent_release_id':complete['release_id'],'parent_package':rec(parent),'canonical_source_sha256':'f3519bacb0efca4077ef93ef21f59e906021585dbd9ae414a37e7bb9b2abf6d0',
        'build_complete':rec(WORK/'build_complete.json'),'object_binding':rec(WORK/'object_binding.json'),'checker':rec(binary),
        'checker_cpp':rec(RESUME/'root_sample_checker_board_a_review_01/sample_add_review.cpp'),'wrapper_result':rec(EXEC/'wrapper_result.json'),
        'checker_checksum_manifest':rec(EXEC/'WRAPPER_SHA256SUMS'),'private_qttest':rec(private),'independent_dependency_report':rec(report),
        'scope':'Qt widget software review; keep application running; physical input, instrument detection, resource and long stability not accepted by this file proof'}
    binding=(json.dumps(basis,ensure_ascii=False,indent=2)+'\n').encode()
    changed={'sn100_shell':binary.read_bytes(),'lib/libQt5Test.so.5':private.read_bytes(),'variant_binding.json':binding,
        'variant_dependencies.json':report.read_bytes()}
    entries={}
    modes={}
    with tarfile.open(parent,'r:gz') as old:
        allmembers=old.getmembers()
        assert len([m for m in allmembers if m.isfile()])==33
        oldsum=old.extractfile('SHA256SUMS').read()
        assert hashlib.sha256(oldsum).hexdigest()==complete['release_id']
        for m in allmembers:
            assert m.isfile() or m.isdir()
            value=PurePosixPath(m.name);assert not value.is_absolute() and '..' not in value.parts
            if not m.isfile() or m.name in ('SHA256SUMS','release_id'):continue
            raw=old.extractfile(m).read();entries[m.name]=raw;modes[m.name]=m.mode
        for row in oldsum.decode().splitlines():
            digest,name=row.split('  ',1);assert hashlib.sha256(entries[name]).hexdigest()==digest
    entries.update(changed)
    modes.update({'sn100_shell':0o755,'lib/libQt5Test.so.5':0o644,'variant_binding.json':0o644,'variant_dependencies.json':0o644})
    sums=''.join(hashlib.sha256(raw).hexdigest()+'  '+name+'\n' for name,raw in sorted(entries.items())).encode()
    release=hashlib.sha256(sums).hexdigest()
    entries['SHA256SUMS']=sums;entries['release_id']=(release+'\n').encode()
    target=OUT/('sn100_sample_review_'+release+'.tar.gz')
    with target.open('xb') as stream:
        with tarfile.open(fileobj=stream,mode='w:gz') as tar:
            for name,raw in sorted(entries.items()):
                info=tarfile.TarInfo(name);info.size=len(raw);info.mode=modes.get(name,0o644);tar.addfile(info,io.BytesIO(raw))
    with tarfile.open(target,'r:gz') as tar:
        assert len(tar.getmembers())==len(entries) and all(m.isfile() for m in tar)
        for m in tar:assert tar.extractfile(m).read()==entries[m.name]
    sourcepack=OUT/'formal15_sources.tar.gz'
    with sourcepack.open('xb') as stream:
        with tarfile.open(fileobj=stream,mode='w:gz') as tar:
            for name,digest in sorted(sources.items()):
                p=ROOT/'06_programs/sn100_host'/name;assert sha(p)==digest
                tar.add(p,arcname=name,recursive=False)
    for p in (WORK/'source_snapshot.json',WORK/'object_binding.json',WORK/'build_complete.json',EXEC/'wrapper_result.json',EXEC/'WRAPPER_SHA256SUMS',report):
        name='checker_WRAPPER_SHA256SUMS' if p.name=='WRAPPER_SHA256SUMS' else ('checker_dependency_report.json' if p==report else p.name)
        destination=OUT/name;shutil.copyfile(p,destination);assert sha(p)==sha(destination)
    final={**basis,'variant_release_id':release,'variant_package':rec(target),'variant_binary_sha256':sha(binary),
        'package_regular_files':len(entries),'source_archive':rec(sourcepack),'compiled':True,'rk_verified':False,'no_formal_source_or_binary_replaced':True}
    put(OUT/'variant_complete.json',final)
    (OUT/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in sorted(OUT.iterdir()) if p.is_file()),encoding='ascii')
    print(json.dumps({'variant_release_id':release,'checker_sha256':sha(binary),'package':rec(target),'members':len(entries),'dependency_files':len(dep['files'])}))
if __name__=='__main__':main()
