"""只更新qmake清单引用；确认实际ARM64编译输入未改变，再重新打包。"""
from pathlib import Path
import hashlib,json,shutil,importlib.util,subprocess,os
w=Path(__file__).resolve().parent;r=w.parents[2];p=r/'06_programs/sn100_host';cache=Path((w/'source_cache_path').read_text().strip())
assert (w/'build_exit_code').read_text().strip()=='0'
identity=json.loads((w/'all_source_identity.json').read_text())
diffs=[name for name,sha in identity.items() if (p/name).suffix!='.md' and hashlib.sha256((p/name).read_bytes()).hexdigest()!=sha]
assert diffs==['shell/shell.pri'],diffs
(w/'initial_all_source_identity.json').write_text(json.dumps(identity,indent=2)+'\n')
(w/'initial_source_snapshot.json').write_bytes((w/'source_snapshot.json').read_bytes())
name='shell/shell.pri';previous=identity[name];shutil.copy2(p/name,cache/name);identity[name]=hashlib.sha256((p/name).read_bytes()).hexdigest()
(w/'all_source_identity.json').write_text(json.dumps(identity,indent=2)+'\n')
spec=importlib.util.spec_from_file_location('pkg',cache/'shell/package_arm64.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
(w/'source_snapshot.json').write_text(json.dumps(m.snapshot(cache),indent=2)+'\n')
env=os.environ.copy();env['SN100_ARM64_LIST_INPUTS']='1';env['SN100_ARM64_BUILD_DIR']=str(w/'arm64_build');env['SN100_ARM64_SYSROOT']=str(r/'temp/sn100_host/original_db_20261027/integration/release_work_02/sysroot')
inputs=subprocess.check_output(['bash',str(cache/'shell/build.sh'),str(w/'unused')],env=env)
assert inputs==(w/'original_main_task_chain_rk_test_build/inputs.tsv').read_bytes()
(w/'qmake_reference_revision.json').write_text(json.dumps({'file':name,'before':previous,'after':identity[name],'reason':'统一qmake清单与试剂共享依赖使用相同规范路径，避免重复对象；该pri不参与ARM64脚本生成输入','actual_arm64_source_header_form_resource_lists_unchanged':True},ensure_ascii=False,indent=2)+'\n')
subprocess.run(['python3',str(cache/'shell/package_arm64.py'),'package','--binary',str(w/'build/sn100_shell'),'--sysroot',env['SN100_ARM64_SYSROOT'],'--snapshot',str(w/'source_snapshot.json'),'--dependencies',str(w/'dependencies/dependency_report.json'),'--output',str(w/'packages')],check=True)
print('FINAL_SNAPSHOT_AND_PACKAGE_MATCH=1')
