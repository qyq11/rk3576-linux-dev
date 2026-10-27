#!/usr/bin/env python3
"""Copy completed post-platform text receipts only; never enter build/runtime/sysroot or rerun a stage."""
import argparse
import datetime
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import traceback
import xml.etree.ElementTree as ET


WORK=Path(__file__).resolve().parents[1]
ROOT=WORK.parents[2]
OUT=ROOT/'04_references/sn100_host/qc_history_integration_20261026/post_platform'
FREEZE='1486686b182dc8d1da55c52b45c91ea72f1b8c41677676cedf16d72b3fc4f401'
STAGES=('project_advanced','project_basis','reagent_batches','reagent_basis','projects_app','reagents_app')
CONTROL={'post_projects_modules_01':(0,28231,'fa58f1'), 'post_standalone_01':(1,45612,'6940c6'),
         'post_standalone_02':(0,95479,'9b6c16'), 'post_response_01':(0,32337,'133e58'),
         'post_resource_01':(0,52932,'a5d8fe')}
EXTENSIONS={'.json','.log','.txt','.xml','.exit','.py','.patch'}


def need(ok,why):
    if not ok:raise RuntimeError(why)


def sha(data):return hashlib.sha256(data).hexdigest()


def text(path):
    path=Path(path)
    need(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(ROOT),'Not ordinary workspace text: '+str(path))
    value=path.read_bytes()
    try:
        need(b'\0' not in value,'NUL');decoded=value.decode('utf-8',errors='strict')
    except (RuntimeError,UnicodeDecodeError) as error:
        raise RuntimeError('TEXT_REVIEW_REQUIRED path='+str(path.relative_to(ROOT))+' bytes='+str(len(value))+' sha256='+sha(value)+' reason='+str(error)) from None
    need(not re.search(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',decoded),'Private key needs review')
    need(not re.search(r'(?i)(?:authorization\s*:\s*bearer\s+|"(?:password_hash|password_salt|access_token|refresh_token)"\s*:\s*"[^"\s]+)',decoded),'Authentication content needs review')
    return value


def read(path):return json.loads(text(path))


def encoded(value):return (json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8')


def add(selected,name,path):
    rel=PurePosixPath(name)
    need(not rel.is_absolute() and '..' not in rel.parts and '\\' not in name,'Unsafe archive path')
    need(not any(x in ('build','runtime','host_runtime','host_environment','executable','sysroot','.obj','.moc','test_data') for x in rel.parts),'Forbidden archive namespace')
    need(name not in selected,'Duplicate archive member: '+name);selected[name]=path


def top(selected,directory,prefix,skipped):
    for path in sorted(directory.iterdir()):
        if path.is_file():
            if path.suffix in EXTENSIONS:add(selected,prefix+'/'+path.name,path)
            else:skipped.append({'path':path.relative_to(ROOT).as_posix(),'bytes_from_stat':path.stat().st_size,'reason':'Not selected metadata/text; payload not opened or hashed'})


def qt(directory,expected):
    txt=text(directory/'qt.txt').decode('utf-8');xml=ET.fromstring(text(directory/'qt.xml'))
    match=re.search(r'^Totals: (\d+) passed, (\d+) failed, (\d+) skipped, (\d+) blacklisted,',txt,re.M)
    need(match and [int(x) for x in match.groups()]==[expected,0,0,0],'Qt totals differ: '+str(directory))
    cases=xml.findall('testcase')
    need(xml.tag=='testsuite' and len(cases)==expected and all(x.get('result')=='pass' for x in cases)
         and not any(xml.findall('.//'+tag) for tag in ('failure','error','skipped')),'Qt testcase outcomes differ')
    declared=read(directory/'qt_verification.json')
    need(declared['txt_sha256']==sha(text(directory/'qt.txt')) and declared['junitxml_sha256']==sha(text(directory/'qt.xml'))
         and declared['totals']==[expected,0,0],'Qt proof fingerprint differs')
    return {'totals':[expected,0,0,0],'xml_root_attributes':dict(xml.attrib),'txt_sha256':sha(text(directory/'qt.txt')),'xml_sha256':sha(text(directory/'qt.xml'))}


def phase(directory,name,expected=0):
    receipt=read(directory/(name+'.exit.json'));command=read(directory/(name+'.command.json'));process=read(directory/(name+'.process.json'))
    need(receipt['exit_code']==int(text(directory/(name+'.exit')).decode('utf-8').strip())==expected
         and receipt['timed_out'] is False and receipt['state']=='finished' and receipt.get('cleanup') is None,'Stage exit failed/unknown')
    need(all(receipt[k]==command[k] for k in ('command','cwd','started_utc')) and process['pid']==receipt['pid'],'Command/process receipt differs')
    return receipt


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--root-go',action='store_true',required=True);parser.add_argument('--attempt',required=True);args=parser.parse_args()
    need(args.root_go and re.fullmatch(r'post_platform_archive_\d{2}',args.attempt),'Explicit go/new attempt needed')
    attempt=WORK/'archive_attempts'/args.attempt;need(not attempt.exists(),'Attempt already exists');attempt.mkdir(parents=True)
    owned=False
    try:
        need(not OUT.exists() and not OUT.is_symlink(),'Formal archive must be new')
        frozen=read(WORK/'native/final_before.json');native=read(WORK/'native/verification.json')
        need(len(frozen)==447 and sha(text(WORK/'native/final_before.json'))==FREEZE
             and native['passed'] is True and native['actual_exit']==0 and native['snapshot_sha256']==FREEZE,'Native/freeze mismatch')
        native_objects={k:v['sha256'] for k,v in native['fresh_objects'].items()};artifacts=native['artifacts']
        selected={};skipped=[];parents={};control_counts={}
        for stage,(expected,session,chunk) in CONTROL.items():
            d=WORK/(stage+'_control');v=read(d/'verification.json');exit_row=read(d/'stage.exit.json');process=read(d/'stage.process.json')
            need(v['actual_stage_exit']==exit_row['actual_stage_exit']==int(text(d/'stage.exit').decode('utf-8').strip())==expected
                 and v['state']==exit_row['state']=='finished' and v['timed_out'] is False and exit_row['timed_out'] is False
                 and v['source_inputs']==447 and v['source_snapshot_sha256']==FREEZE and v['passed'] is (expected==0),'Control stage actual outcome differs')
            need(v['pid']==exit_row['pid']==process['pid'] and v['command']==exit_row['command']==process['command'],'Control process identity differs')
            cleanup=v['recorded_group_cleanup'];need(cleanup['all_gone'] is True and cleanup['signals_used'] is False
                 and v['outer_group_after']['gone'] is True and all(x['after']['gone'] is True for x in cleanup['groups']),'Control groups not proven gone')
            b=read(d/'binding_before.json')
            need(b['verified_actual_source_sha256']==frozen and b['verified_actual_native_objects_sha256']==native_objects
                 and b['verified_actual_artifacts']==artifacts and b['source_snapshot_sha256']==FREEZE,'Control binding differs')
            if expected==0:need(read(d/'binding_after.json')==b,'Control inputs changed')
            control_counts[stage]={'actual_exit':expected,'owned_recorded_groups':len(cleanup['groups']),'signals_used_for_cleanup':False}
            parents[stage]={'observation_type':'retrospective root collaboration report of tool completion; not independently OS-emitted receipt',
                'root_exec_session':session,'actual_parent_exit':expected,'tool_completion_chunk':chunk,
                'os_parent_pid_not_captured':True,'complete_environment_not_reconstructed':True,'parent_full_argv_not_reconstructed':True}
            top(selected,d,stage+'_control',skipped)
            top(selected,WORK/stage,stage,skipped)
        for name in ('post_platform_preparation','post_platform_preparation_02'):
            d=WORK/name;top(selected,d,name,skipped);proof=read(d/'preparation.json')
            need(proof['source_inputs']==447 and proof['source_snapshot_sha256']==FREEZE and proof['native_artifacts']==artifacts,'Preparation belongs to another freeze')
            for rel,expected in read(d/'files_sha256.json').items():need(sha(text(d/rel))==expected,'Private prep changed: '+name+'/'+rel)
            for path,expected in proof['executed_inputs_sha256'].items():
                prefix='/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/'
                if path.startswith(prefix+'temp/') and '/post_platform_preparation' in path:
                    need(sha(text(ROOT/path[len(prefix):]))==expected,'Executed private controller changed')
        projects=WORK/'post_projects_modules_01';pv=read(projects/'verification.json')
        need(pv['passed'] is True and pv['source_inputs']==447 and pv['source_snapshot_sha256']==FREEZE and pv['main_artifacts']==artifacts
             and all(pv['checks'].values()) and [x['name'] for x in pv['stages']]==list(STAGES),'Projects/modules summary differs')
        need(read(projects/'source_before.json')==read(projects/'source_after.json')==frozen,'Projects source changed')
        binding=read(projects/'binding_before.json')
        need(binding==read(projects/'binding_after.json')==read(projects/'binding_observed_final.json')
             and binding['native_objects_sha256']==native_objects and binding['main_artifacts']==artifacts,'Projects binding changed')
        need(read(projects/'test_inputs_before.json')==read(projects/'test_inputs_after.json'),'Projects tests changed')
        qt_results={};phase_count=0
        expected_tests={'project_advanced':11,'project_basis':9,'reagent_batches':11,'reagent_basis':11}
        for row in pv['stages']:
            d=projects/row['name'];top(selected,d,'post_projects_modules_01/'+row['name'],skipped);top(selected,d/'logs','post_projects_modules_01/'+row['name']+'/logs',skipped)
            need(read(d/'verification.json')==row and row['passed'] is True,'Module stage proof differs')
            phases=('qmake','make','test' if row['name'] in expected_tests else 'app')
            need([x['name'] for x in row['phases']]==list(phases),'Module phases differ')
            for item in row['phases']:
                raw=phase(d/'logs',item['name']);need(raw==item,'Embedded phase differs');phase_count+=1
            if row['name'] in expected_tests:qt_results[row['name']]=qt(d,expected_tests[row['name']])
        need(len(pv['standalone'])==2 and all(x['actual_exit']==0 and x['timer_ms']==1800 and x['timeout_cleanup_used'] is False for x in pv['standalone']),'Two app startup outcomes differ')
        failed=read(WORK/'post_standalone_01/failure.json')
        need(failed['error']=='The repaired ProjectsStore was not a source of this target'
             and set(failed['actual_stages_completed'])=={'quality_control_qmake','quality_control_make','calibration_qmake','calibration_make'},'Original failed-controller scope differs')
        for name in ('post_standalone_01','post_standalone_02'):
            d=WORK/name;top(selected,d/'logs',name+'/logs',skipped)
            v=failed if name.endswith('01') else read(d/'verification.json')
            phases=v['actual_stages_completed'] if name.endswith('01') else v['stages']
            for label,item in phases.items():
                raw=phase(d/'logs',label)
                need(all(item[k]==value for k,value in raw.items()),'Standalone raw phase differs')
                extra=set(item)-set(raw)
                if label=='calibration_catalog_owner':
                    need(extra=={'checks','closure_marker_counts'}
                         and item==read(d/'logs/calibration_catalog_owner.verification.json')
                         and all(item['checks'].values()),'Catalog-owner supplementary final proof differs')
                else:need(not extra,'Unexpected supplementary standalone phase data')
        standalone=read(WORK/'post_standalone_02/verification.json')
        need(standalone['passed'] is True and standalone['source_inputs']==447 and standalone['source_snapshot_sha256']==FREEZE
             and standalone['main_artifacts']==artifacts and standalone['old01_build_artifacts_reused'] is False
             and standalone['total_phase_count']==len(standalone['stages'])==7 and all(standalone['checks'].values())
             and standalone['source_before']==standalone['source_after']==frozen,'Standalone02 scope/binding differs')
        response=WORK/'post_response_01';rv=read(response/'verification.json');metrics=read(response/'evidence/metrics.json')
        add(selected,'post_response_01/evidence/metrics.json',response/'evidence/metrics.json')
        phase(response,'response');qt_results['response']=qt(response,3)
        need(rv['passed'] is True and rv['actual_exit']==0 and rv['sample_count']==len(metrics['samples'])==40
             and rv['metrics_sha256']==sha(text(response/'evidence/metrics.json')) and metrics['test_function_passed'] is True,'Response evidence differs')
        binding_proof=WORK/'cumulative_build/binding_proof.json'
        need(sha(text(binding_proof))==rv['bound_proof_sha256'],'Response cumulative binding differs')
        for label,path in (('native/verification.json',WORK/'native/verification.json'),('native/final_before.json',WORK/'native/final_before.json'),
                           ('cumulative/binding_proof.json',binding_proof),('cumulative/verification.json',WORK/'cumulative_build/verification.json')):
            add(selected,'binding_reference/'+label,path)
        for key,field in (('queued_to_busy','first_busy_ns'),('queued_to_finished','operation_finished_ns'),('queued_to_committed_page','committed_page_observed_ns')):
            values=[]
            for x in metrics['samples']:
                value=(x[field]-x['queued_ns'])/1e6;need(abs(value-x[key+'_ms'])<1e-8 and x['success'] is True,'Response arithmetic differs');values.append(value)
            values.sort();recomputed={'count':len(values),'p95_ms':values[math.ceil(.95*len(values))-1],'p99_ms':values[math.ceil(.99*len(values))-1],'max_ms':values[-1]}
            need(all(abs(rv['arithmetic'][key][k]-v)<1e-8 for k,v in recomputed.items()),'Response aggregate arithmetic differs')
        resource=read(WORK/'post_resource_01/verification.json');summary=read(WORK/'post_resource_01/summary.json');samples=read(WORK/'post_resource_01/samples.json')
        observer=read(WORK/'post_resource_01_driver/observer.exit.json');top(selected,WORK/'post_resource_01_driver','post_resource_01_driver',skipped)
        need(resource['passed'] is True and all(resource['checks'].values()) and resource['source_input_count']==447
             and resource['source_sha256_before']==resource['source_sha256_after']==frozen
             and resource['binary_sha256_before']==resource['binary_sha256_after']==artifacts,'Whole-product resource binding differs')
        need(resource['actual_observer_exit_code']==resource['actual_child_exit_code']==observer['actual_observer_exit_code']==observer['actual_child_exit_code']==summary['exit_code']==0
             and summary['timed_out'] is False and summary['ordinary_sigterm_requested'] is True
             and observer['group_after_observer']['verified_stopped'] is True and summary['sample_count']==len(samples)
             and all(samples[i]['elapsed_seconds']<samples[i+1]['elapsed_seconds'] for i in range(len(samples)-1)),'Resource lifecycle/sampling receipt differs')
        add(selected,'collection_source/archive_post_platform.py',Path(__file__))
        for suffix in ('.command.json','.process.json','.exit.json','.stdout.log','.stderr.log'):
            name='post_platform_archive_collection_01'+suffix
            add(selected,'collection_history/attempt_01/'+name,WORK/'integration_drivers'/name)
        for name in ('failure.json','archive_post_platform_executed.py'):
            add(selected,'collection_history/attempt_01/'+name,WORK/'archive_attempts/post_platform_archive_01'/name)
        before={name:{'source':path.relative_to(ROOT).as_posix(),'bytes':len(text(path)),'sha256':sha(text(path))} for name,path in sorted(selected.items())}
        OUT.mkdir();owned=True
        for name,path in sorted(selected.items()):
            value=text(path);item=before[name];need(sha(value)==item['sha256'] and len(value)==item['bytes'],'Input changed before copy')
            dest=OUT/name;dest.parent.mkdir(parents=True,exist_ok=True)
            with dest.open('xb') as handle:handle.write(value)
            need(text(dest)==value,'Copied bytes differ')
        need({name:{'source':path.relative_to(ROOT).as_posix(),'bytes':len(text(path)),'sha256':sha(text(path))} for name,path in sorted(selected.items())}==before,'Selected originals changed')
        (OUT/'copied_inputs.json').write_bytes(encoded(before));(OUT/'root_parent_observations.json').write_bytes(encoded(parents))
        counts={'copied_original_text_files':len(before),'copied_original_bytes':sum(x['bytes'] for x in before.values()),'source_inputs':447,
                'control_stages':control_counts,'project_module_stages':len(pv['stages']),'project_module_phases':phase_count,
                'standalone01_actual_build_phases':len(failed['actual_stages_completed']),'standalone02_actual_phases':len(standalone['stages']),
                'response_samples':len(metrics['samples']),'resource_samples':len(samples)}
        proof={'schema':'sn100-post-platform-text-archive-v1','passed':True,'counts':counts,'source_snapshot_sha256':FREEZE,
               'native_verification_sha256':sha(text(WORK/'native/verification.json')),'qt_results_with_original_xml_attributes':qt_results,
               'checks':{'raw_actual_exit_command_process_crosschecked':True,'source_native_controller_metadata_bound':True,
                         'response_samples_and_aggregate_recomputed':True,'copied_originals_sha_and_length_equal':True,'original_inputs_unchanged':True},
               'response_committed_ms':rv['arithmetic']['queued_to_committed_page'],
               'whole_product_resource':{'elapsed_seconds':summary['elapsed_seconds'],'cpu_percent_one_logical_core_including_startup':summary['cpu_percent_one_logical_core'],
                    'maximum_threads':summary['maximum_Threads'],'maximum_fd_count':summary['maximum_fd_count'],'ordinary_observer_planned_sigterm':True,'forced_cleanup_used':False},
               'scope':'WSL offline supplement only. Response p95 exceeds100ms candidate; candidate/user budget not approved. Main CPU includes startup;10<=t<30 is fixed observer-clock window, not proven READY aligned. VmHWM lifetime peak. No RK/instrument/compositor or long stability claim; no comparison with single-page history resource.',
               'excluded_payloads':skipped,'production_or_test_source_payloads_not_copied':True,'objects_executables_runtime_database_ini_credentials_not_read_or_copied':True,
               'photos':'Metadata-only: original PNG payloads deliberately not opened/copied; their existing producer proof remains raw.',
               'quiet_period_basis':'Root task reports all3 chats and internal agents acknowledged silence with zero child work during measurement; not independently reconstructed by archiver.'}
        (OUT/'archive_verification.json').write_bytes(encoded(proof))
        handoff={'schema':'sn100-post-platform-handoff-v1','status':'text_evidence_archived','source_snapshot_sha256':FREEZE,'counts':counts,
                 'original_standalone01_failure_preserved':True,'parent_observations':'root_parent_observations.json','copy_index':'copied_inputs.json',
                 'archive_checks':'archive_verification.json','scope':proof['scope'],'original_xml_diagnostic_attributes_retained':True,
                 'raw_stage_exit_passed_false_is_not_rewritten':'Some early stage.exit.json records retain passed=false before the later controller proof; actual_stage_exit and final verification are separately checked.',
                 'no_existing_evidence_or_preparation_modified':True}
        (OUT/'handoff.json').write_bytes(encoded(handoff))
        hashes={p.relative_to(OUT).as_posix():{'bytes':len(text(p)),'sha256':sha(text(p))} for p in sorted(OUT.rglob('*')) if p.is_file()}
        (OUT/'SHA256SUMS').write_text(''.join(v['sha256']+'  '+k+'\n' for k,v in hashes.items()),encoding='utf-8',newline='\n')
        need(all(len(text(OUT/k))==v['bytes'] and sha(text(OUT/k))==v['sha256'] for k,v in hashes.items()),'Manifest reread differs')
        result={'passed':True,'files':len(hashes)+1,'sha_entries':len(hashes),'counts':counts,'handoff_sha256':sha(text(OUT/'handoff.json')),'sha256sums_sha256':sha(text(OUT/'SHA256SUMS'))}
        (attempt/'result.json').write_bytes(encoded(result));print(json.dumps(result,ensure_ascii=False))
    except Exception as error:
        failure={'passed':False,'error':str(error),'traceback':traceback.format_exc(),'formal_archive_created_by_this_attempt':owned,'no_business_stage_rerun':True}
        (attempt/'failure.json').write_bytes(encoded(failure))
        if owned:(OUT/'INCOMPLETE.json').write_bytes(encoded(failure))
        raise


if __name__=='__main__':main()
