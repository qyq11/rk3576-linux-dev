from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,time
work=Path(__file__).resolve().parent
root=work.parents[2]
program=root/'06_programs/sn100_host'
attempt=work/'attempts/final_03'
target=work/'corrected_original'
run_label=os.environ.get('SN100_ORIGINAL_RUN_LABEL','')
assert not run_label or (run_label.startswith('_') and run_label[1:].isalnum())
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def save(path,value): path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
if sys.argv[1]=='build':
    assert not target.exists()
    target.mkdir()
    sources=json.loads((attempt/'source_manifest.json').read_text())
    destination=target/'source_snapshot'
    for item in sources['files']:
        source=attempt/'source_snapshot'/item['path']
        assert sha(source)==item['sha256']
        output=destination/item['path']
        output.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,output)
    old=(destination/'tests/shell_account_test.cpp').read_text(encoding='utf-8-sig')
    before='''        QVERIFY(session.navigationClick("btnHospInfo", 6, 1, &probe));
        QVERIFY(session.navigationClick("btnAccount", 6, 0, &probe));'''
    after='''        QVERIFY(session.navigationClick("btnHospInfo", 6, 1, &probe));
        // QWidget QTest injection and navigationChanged are synchronous.
        // Let queued paints, retained animations and heartbeat timers run
        // between user actions instead of manufacturing one long GUI burst.
        QTest::qWait(10);
        QVERIFY(session.navigationClick("btnAccount", 6, 0, &probe));
        QTest::qWait(10);'''
    assert old.count(before)==1
    current=(program/'tests/shell_account_test.cpp').read_text(encoding='utf-8-sig')
    assert old.replace(before,after)==current, 'Only the authorized pacing change is allowed'
    (destination/'tests/shell_account_test.cpp').write_text(current,encoding='utf-8',newline='\n')
    pro=program/'tests/shell_account_test.pro'
    old_manifest=json.loads((root/'04_references/sn100_host/offline_shell_account_20261023/source_manifest.json').read_text())
    expected=next(item['sha256'] for item in old_manifest if item['path']=='06_programs/sn100_host/tests/shell_account_test.pro')
    assert sha(pro)==expected
    shutil.copy2(pro,destination/'tests/shell_account_test.pro')
    build=target/'build'
    build.mkdir()
    with (target/'build.log').open('x',encoding='utf-8') as log:
        subprocess.run(['/usr/lib/qt5/bin/qmake',str(destination/'tests/shell_account_test.pro'),'-spec','linux-g++','CONFIG+=release','CONFIG-=debug'],cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
        subprocess.run(['make','-j4'],cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
    binary=build/'shell_account_test'
    save(target/'build_metadata.json',{'configuration':'WSL Release Qt5.15.13; exact final_03 frozen product sources, original comprehensive test with two pacing waits',
        'base_product_source_manifest_sha256':sha(attempt/'source_manifest.json'),
        'corrected_test_sha256':sha(destination/'tests/shell_account_test.cpp'),
        'unchanged_pro_sha256':sha(pro),'binary_sha256':sha(binary),
        'product_changes':'None; only original test action pacing changed. All original assertions/counts retained.'})
    print('Corrected original test built from unchanged frozen product sources.',flush=True)
elif sys.argv[1]=='run':
    assert (target/'build_metadata.json').is_file()
    binary=target/'build/shell_account_test'
    metadata=json.loads((target/'build_metadata.json').read_text())
    assert sha(binary)==metadata['binary_sha256']
    runs=[]
    for index in range(1,4):
        data=root/f'temp/sn100_host/o03_2/heartbeat_fixed{run_label}_{index:02d}'
        assert not data.exists(), 'Corrected regression must never reuse previous data'
        env=dict(os.environ,QT_QPA_PLATFORM='xcb',SN100_SHELL_ACCOUNT_EVIDENCE_DIR=str(data))
        for slot in ['writeExperiment','readExperiment']:
            path=target/f'{slot}{run_label}_{index:02d}.log'
            started=time.monotonic()
            with path.open('x',encoding='utf-8') as output:
                result=subprocess.run([str(binary),slot],env=env,stdout=output,stderr=subprocess.STDOUT,timeout=100)
            runs.append({'round':index,'slot':slot,'data_root':str(data),'log':str(path),'exit_code':result.returncode,'elapsed_seconds':time.monotonic()-started})
            save(target/f'runs_manifest{run_label}.json',runs)
            print(index,slot,result.returncode,round(time.monotonic()-started,3),flush=True)
            assert result.returncode==0,path
    print('Corrected original comprehensive writes and independent reads passed in all three rounds.',flush=True)
else:
    raise SystemExit('build or run')
