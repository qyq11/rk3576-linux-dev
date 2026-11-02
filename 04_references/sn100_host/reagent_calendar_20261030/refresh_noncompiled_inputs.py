from pathlib import Path
import hashlib,json,shutil,subprocess,importlib.util,os
w=Path(__file__).resolve().parent;r=w.parents[2];p=r/'06_programs/sn100_host';cache=Path((w/'source_cache_path').read_text().strip());identity=json.loads((w/'all_source_identity.json').read_text())
allowed={'reagents/build.sh','reagents/reagents.pri','shell/shell.pri','samples/tests/original_main_task_chain_rk_test.cpp'}
diffs={name for name,sha in identity.items() if (p/name).suffix!='.md' and hashlib.sha256((p/name).read_bytes()).hexdigest()!=sha};assert diffs==allowed,diffs
revision={}
for name in sorted(diffs):
    after=hashlib.sha256((p/name).read_bytes()).hexdigest();revision[name]={'before':identity[name],'after':after};shutil.copy2(p/name,cache/name);identity[name]=after
(w/'all_source_identity.json').write_text(json.dumps(identity,indent=2)+'\n')
spec=importlib.util.spec_from_file_location('pkg',cache/'shell/package_arm64.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
(w/'source_snapshot.json').write_text(json.dumps(m.snapshot(cache),indent=2)+'\n')
env=os.environ.copy();env['SN100_ARM64_LIST_INPUTS']='1';env['SN100_ARM64_BUILD_DIR']=str(w/'arm64_build');env['SN100_ARM64_SYSROOT']=str(r/'temp/sn100_host/original_db_20261027/integration/release_work_02/sysroot')
inputs=subprocess.check_output(['bash',str(cache/'shell/build.sh'),str(w/'unused')],env=env);assert inputs==(w/'original_main_task_chain_rk_test_build/inputs.tsv').read_bytes()
(w/'post_compile_input_revision.json').write_text(json.dumps({'changed_noncompiled_files':revision,'actual_unified_arm64_compile_inputs_unchanged':True,'reason':'补齐独立试剂入口既有原库对OriginalIni的链接依赖；规范qmake引用；测试S_1改SD_1。统一程序的源文件、头、表单和资源字节均未改变。'},ensure_ascii=False,indent=2)+'\n')
subprocess.run(['python3',str(cache/'shell/package_arm64.py'),'package','--binary',str(w/'build/sn100_shell'),'--sysroot',env['SN100_ARM64_SYSROOT'],'--snapshot',str(w/'source_snapshot.json'),'--dependencies',str(w/'dependencies/dependency_report.json'),'--output',str(w/'packages')],check=True)
