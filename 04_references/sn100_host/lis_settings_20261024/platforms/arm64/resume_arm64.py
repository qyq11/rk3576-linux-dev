from pathlib import Path
import importlib.util,json,os,shutil,subprocess,time

base=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('arm_driver',base/'run_arm64.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
assert not (base/'verification.json').exists() and not (base/'resume_inputs.json').exists()
frozen=m.readjson(m.FREEZE)
assert frozen==m.readjson(base/'source_before.json')
assert all(m.digest(m.PROGRAM/path)==digest for path,digest in frozen.items())
assert m.sysroot_inputs()==m.readjson(base/'sysroot_before.json')
assert subprocess.check_output(['/usr/bin/aarch64-linux-gnu-g++','-dumpfullversion'],text=True).strip()==(base/'compiler_version.log').read_text().strip()
receipts=[dict(m.readjson(path),receipt_file=path.relative_to(base).as_posix()) for path in (base/'compiler_receipts').glob('*.json')]
good={row['output']:row for row in receipts if row.get('exit_code')==0}
assert len(good)==126 and len(receipts)==128,(len(good),len(receipts))
for row in good.values():
    assert m.digest(row['output'])==row['output_sha256']
    row['source_sha256']=m.digest(row['source'])
headers={str(path):m.digest(path) for path in m.BUILD.glob('ui_*.h')}
generated={str(path):m.digest(path) for path in m.BUILD.glob('*.cpp')}
info={'reusable':good,'generated_headers':headers,'generated_sources':generated,
      'source_sha256':m.digest(m.FREEZE),'compiler_sha256':m.digest('/usr/bin/aarch64-linux-gnu-g++'),
      'interruption':'Original build log records two Hangup commands at obj126/127; no completed build exit receipt; no active compiler remains; exact external cause unknown',
      'interrupted_receipts':[row['receipt_file'] for row in receipts if row.get('exit_code') is None],
      'incomplete_objects_reused':False}
m.save('resume_inputs.json',info)
trace=base/'resume_trace.bash'
trace.write_text('if [[ $0 == */sn100_host/shell/build.sh ]]; then\nfunction aarch64-linux-gnu-g++() { /usr/bin/python3 "'+str(base/'resume_compiler.py')+'" "$@"; }\nfi\n')
env=dict(os.environ,**m.readjson(base/'prebuild_inputs.json')['build_environment'],BASH_ENV=str(trace))
# Preserve original build.log and every completed/incomplete compiler receipt.
m.step('build_resume',['bash',str(m.PROGRAM/'shell/build.sh'),str(m.BUILD/'sn100_shell')],env=env)
assert all(m.digest(path)==digest for path,digest in headers.items())
assert all(m.digest(path)==digest for path,digest in generated.items())
all_rows=[dict(m.readjson(path),receipt_file=path.relative_to(base).as_posix()) for path in (base/'compiler_receipts').glob('*.json')]
complete=[row for row in all_rows if row.get('exit_code')==0]
assert all(row.get('exit_code') in (None,0) for row in all_rows)
outputs=[row['output'] for row in complete]
assert len(outputs)==len(set(outputs))
compile_rows={row['output']:row for row in complete if 'source' in row}
links=[row for row in complete if 'source' not in row]
assert len(links)==2
for row in links:assert row['output_sha256']==m.digest(Path(row['output']).with_suffix(''))
m.save('compiler_receipt_summary.json',{'compile_invocations':len(compile_rows),'link_invocations':len(links),
    'completed_invocations':len(complete),'interrupted_invocations':info['interrupted_receipts'],
    'resumed_objects':len(compile_rows)-len(good),'verified_reused_this_batch_objects':len(good),
    'old_checkpoint_objects_reused':0,'all_completed_exit_codes_zero':True,'links':links})
start_ns=min(row['started_ns'] for row in receipts)-1
object_summary=m.audit_objects(start_ns,compile_rows)
m.step('snapshot_after_build',['python3','-B',str(m.PROGRAM/'shell/package_arm64.py'),'snapshot','--output',str(base/'source_after_build.json')])
assert m.readjson(base/'source_after_build.json')==frozen
m.step('closure',['python3','-B',str(m.PROGRAM/'shell/check_arm64.py'),str(m.BUILD/'sn100_shell'),
    '--additional-binary',str(m.BUILD/'sn100_report_renderer'),'--sysroot',str(m.SYSROOT),'--output-dir',str(base/'dependencies')])
closure=m.readjson(base/'dependencies/dependency_report.json')
assert closure['offline_link_and_sql_closure_passed'] and not closure['errors'] and not closure['board_runtime_verified']
m.step('package',['python3','-B',str(m.PROGRAM/'shell/package_arm64.py'),'package','--binary',str(m.BUILD/'sn100_shell'),
    '--sysroot',str(m.SYSROOT),'--snapshot',str(m.FREEZE),'--dependencies',str(base/'dependencies/dependency_report.json'),'--output',str(base/'packages')])
package=m.audit_package(frozen)
source_package=m.make_source_package(frozen,package['release_id'])
m.step('snapshot_after_package',['python3','-B',str(m.PROGRAM/'shell/package_arm64.py'),'snapshot','--output',str(base/'source_after_package.json')])
assert m.readjson(base/'source_after_package.json')==frozen
sysroot_after=m.sysroot_inputs();m.save('sysroot_after.json',sysroot_after)
assert sysroot_after==m.readjson(base/'sysroot_before.json')
result={'schema':'sn100-lis-arm64-resumed-build-offline-v1','finished_utc':m.now(),'source_entries':len(frozen),
    'source_before_after_and_package_equal_native_freeze':True,'build_exit_code':0,'closure_exit_code':0,'package_exit_code':0,
    'original_build_exit_code':None,'original_interrupted_at_objects':[126,127],
    'verified_reused_this_batch_objects':126,'old_checkpoint_objects_reused':0,'full_object_recompilation':True,
    'full_object_scope':'All final objects freshly compiled for this371 batch;126 actual completed receipts reused after external Hangup interruption, remaining units compiled on resume; no completed original build receipt invented',
    'sysroot_reused_read_only':str(m.SYSROOT),'sysroot_fingerprint_unchanged':True,'object_summary':object_summary,
    'main_sha256':m.digest(m.BUILD/'sn100_shell'),'renderer_sha256':m.digest(m.BUILD/'sn100_report_renderer'),
    'closure_elf_count':len(closure['files']),'closure_errors':closure['errors'],'package':package,'source_package':source_package,
    'board_runtime_verified':False,'real_printer_or_instrument_used':False,'exact_per_invocation_receipts':True,
    'parallel_xtrace_is_not_argv_proof':True,'runtime_data_or_credentials_archived':False}
m.save('steps.json',m.STEPS);m.save('verification.json',result)
print(json.dumps(result),flush=True)
