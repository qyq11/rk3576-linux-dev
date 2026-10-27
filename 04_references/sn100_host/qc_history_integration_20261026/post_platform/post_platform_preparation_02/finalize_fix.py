"""Finish static preparation only; never launch qmake, an app, or a resource observer."""
import ast
import difflib
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE.parent
ROOT = WORK.parents[2]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def save(name,value):
    (HERE/name).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


def main():
    plan = load(HERE/'preparation.json')
    checks = load(HERE/'role_checks.json')
    assert checks['passed'] and checks['positive_captured_graphs']==8 and checks['negative_cases']==81
    assert checks['classifier_sha256']==sha(HERE/'build_roles.py')
    assert checks['checker_sha256']==sha(HERE/'check_captured_roles.py')
    for path,digest in plan['executed_inputs_sha256'].items():
        assert sha(Path(path.replace('/mnt/c/','C:/',1)))==digest
    parsed = []
    for path in sorted(HERE.glob('*.py')):
        ast.parse(path.read_text(encoding='utf-8'))
        parsed.append(path.name)
    helper = (HERE/'build_verification.py').read_text(encoding='utf-8')
    assert 'role_proof = verify_role(graph)' in helper and "'target_role': role_proof" in helper
    assert 'The repaired ProjectsStore was not a source of this target' not in helper
    runner = (HERE/'run_standalone.py').read_text(encoding='utf-8')
    assert 'from build_verification import verify_build, build_inventory' in runner
    assert "'standalone_app_count': 2" in runner and "'total_phase_count': 7" in runner
    assert not (WORK/'post_standalone_02').exists() and not (WORK/'post_standalone_02_control').exists()
    old = load(HERE/'preservation_before.json')['non_runtime_files_sha256']
    assert all(sha(ROOT/path)==digest for path,digest in old.items())
    plan.update(status='prepared_static_checks_passed',static_positive_graphs=8,static_negative_cases=81,
                role_check_source_bound=True,executed_build_or_GUI=False)
    save('preparation.json',plan)
    save('review.json',{
        'schema':'sn100-standalone-target-role-fix-review-v2','status':'static_preparation_only',
        'old_failure':'项目/试剂专用ProjectsStore要求误套到校准目标；属于验证器判断错误。',
        'old01_accounting':{'four_qmake_make_actual_exits':plan['old01_actual_compilation_exits'],
            'actual_stage_exit':plan['old01_stage_actual_exit'],'page_start_invocations':0,
            'startup_or_close_acceptance':False,'original_evidence_modified':False},
        'new_role_checks':{
            'QC':'正向要求qc_history_core、statistics、qc_store、quality_control_page、qc_history_presenter、两图形源和main，并确认它们实际映射到参与链接的对象。',
            'calibration':'正向要求calibration_store、calibration_page、calibrationchart及main；不套ProjectsStore要求。',
            'projects_reagents':'6个已知项目/试剂app及test目标保留ProjectsStore正向要求，且各自还要求其真实业务页面/存储/入口。',
            'scope_of_rejection':'未知target、缺业务源、业务源无实际对象、声明源与链接对象编译规则不一致均失败。'},
        'captured_graph_validation':{'positive':8,'negative':81,
            'current447_failed_attempt_fixtures':'QC与Cal的实际捕获Makefile；只读验证源/对象角色，不重分类其启动结果。',
            'historical426_fixtures':'另6份项目/试剂Makefile仅验证角色分类器和ProjectsStore拒绝条件，不作为当前447的编译或回归证据。'},
        'fresh02_choice':plan['reason_for_fresh02'],
        'new02_accounting':'尚未编译或启动。将新建02运行/控制目录，重新qmake/make并登记真实时间、对象、源、Makefile和编译收据；不复用01二进制或对象。',
        'standalone_count':'本阶段仅QC/Cal两个独立程序，另含一次catalog所有者初始化；7阶段。项目阶段另外包含Projects/Reagents两启动，合计4个独立程序。',
        'unchanged':'所有06、公共文档、旧准备、旧失败、旧控制目录以及原测量准备均未写入。'})
    save('static_qa.json',{'passed':True,'checks':'8 positive +81 negative captured graph cases; classifier/checker SHA bound',
        'python_AST_parsed':parsed,'fresh_build_output_paths_absent':plan['new_output_paths_absent'],
        'original_non_runtime_files_sha_verified_unchanged':len(old),
        'runtime_values_read_hashed_or_archived':False,'new02_compilation_or_GUI':False,
        'actual_static_check_tool_exit':0,'actual_static_check_tool_chunk':'19a1c3'})
    for name in ('common.py','execute_stage.py','run_standalone.py'):
        old_source = (WORK/'post_platform_preparation'/name).read_text(encoding='utf-8').splitlines(True)
        new_source = (HERE/name).read_text(encoding='utf-8').splitlines(True)
        (HERE/(name+'.patch')).write_text(''.join(difflib.unified_diff(old_source,new_source,
            fromfile='preserved01/'+name,tofile='new02/'+name)),encoding='utf-8')
    (HERE/'RUN_COMMAND.txt').write_text('Prepared only; root must grant a fresh compile/GUI slot.\n'+' '.join(plan['command'])+'\n',encoding='utf-8')
    manifest = {path.name:sha(path) for path in sorted(HERE.iterdir()) if path.is_file() and path.name!='files_sha256.json'}
    save('files_sha256.json',manifest)
    print(json.dumps({'status':plan['status'],'fresh_build_required':True,'old01_objects_reused':False,
        'static_positive':8,'static_negative':81,'standalone_apps':2,'future_phases':7,
        'original_files_unchanged':len(old),'new02_build_or_GUI':False,
        'preparation_sha256':sha(HERE/'preparation.json'),'command':plan['command']},indent=2))


if __name__=='__main__':
    main()
